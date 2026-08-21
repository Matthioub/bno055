/*
  Pulsera háptica ESP32-C3 — todo en un archivo, modularizado por funciones
  ---------------------------------------------------------------------------
  - Lee orientación/magnetómetro del BNO055 y lo manda por BLE al teléfono.
  - Recibe por BLE el estado del semáforo que la app obtiene de Firebase.
  - Traduce ese estado en un patrón de vibración en el DRV2605L.

  Estructura del archivo:
    1) Defines (pines, UUIDs, tiempos, intensidades)
    2) Objetos y variables globales
    3) Funciones del sensor (BNO055)
    4) Funciones de BLE
    5) Funciones de vibración (DRV2605L)
    6) setup() / loop()
*/

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <Adafruit_DRV2605.h>
#include <NimBLEDevice.h>
#include <math.h>

// =============================================================
// 1) DEFINES
// =============================================================

// ---- Pines I2C (bus compartido: BNO055 + DRV2605L) ----
#define PIN_SDA 21
#define PIN_SCL 20

// ---- Medición de batería ----
#define PIN_BATERIA 5
#define RESISTENCIA_BATERIA_A_PIN_OHMS 47000.0f
#define RESISTENCIA_PIN_A_GND_OHMS 10000.0f
#define VOLTAJE_BATERIA_LLENA 4.20f
#define VOLTAJE_BATERIA_VACIA 3.00f
#define CANTIDAD_MUESTRAS_BATERIA 16
#define INTERVALO_MEDICION_BATERIA 10000UL

// ---- BLE ----
#define BLE_NOMBRE "Pulsera Cruzar"
#define BLE_UUID_SERVICIO "12345678-1234-1234-1234-123456789abc"
#define BLE_UUID_CARACTERISTICA "abcd1234-1234-1234-1234-abcdef123456"
#define BLE_UUID_CARACTERISTICA_SEMAFORO "abcd1234-1234-1234-1234-abcdef123457"
#define BLE_UUID_SERVICIO_BATERIA "180F"
#define BLE_UUID_NIVEL_BATERIA "2A19"

// ---- Tiempos generales ----
#define TIEMPO_ESPERA_SETUP 1000  // reintento de sensores no encontrados
#define INTERVALO_ENVIO_BLE 50    // 50ms = 20 datos/seg

// ---- Vibración: intensidades para modo Real-Time Playback (0-127) ----
#define INTENSIDAD_2 51   // 2/5
#define INTENSIDAD_3 76   // 3/5
#define INTENSIDAD_4 102  // 4/5
#define INTENSIDAD_5 127  // 5/5

// =============================================================
// 2) OBJETOS Y VARIABLES GLOBALES
// =============================================================

// ---- Sensor ----
Adafruit_BNO055 bno(55, 0x29);  // 55 = id interno ; 0x29 = dirección I2C

// ---- BLE ----
NimBLEServer* servidor = nullptr;
NimBLECharacteristic* caracteristicaMensaje = nullptr;
NimBLECharacteristic* caracteristicaSemaforo = nullptr;
NimBLECharacteristic* caracteristicaBateria = nullptr;
volatile bool telefonoConectado = false;
volatile bool notificacionesHabilitadas = false;
unsigned long ultimoEnvioBLE = 0;

// ---- Batería ----
uint8_t porcentajeBateriaActual = 0;
unsigned long ultimaMedicionBateria = 0;

// ---- Vibración ----
Adafruit_DRV2605 drv;

enum EstadoSemaforo { SIN_SEMAFORO,
                      SIN_SENAL,
                      ROJO,
                      AMARILLO,
                      VERDE,
                      CAMINANDO };

bool interpretarComando(const char* comando, EstadoSemaforo& resultado);
void vibracionSetEstado(EstadoSemaforo nuevoEstado);
bool vibracionEstaActiva();

struct PasoVibracion {
  unsigned long duracionMs;
  uint8_t intensidad;
};

struct PatronVibracion {
  const PasoVibracion* pasos;
  uint8_t cantidadPasos;
  unsigned long pausaEntrePatronesMs;
};

// Cada lista alterna vibración y silencio. Una intensidad de 0 representa
// silencio. Las formas son diferentes incluso si la intensidad se percibe mal.
const PasoVibracion pasosSinSemaforo[] = {
  { 100, INTENSIDAD_2 },
  { 250, 0 },
  { 100, INTENSIDAD_2 },
};

const PasoVibracion pasosSinSenal[] = {
  { 160, INTENSIDAD_5 },
  { 100, 0 },
  { 160, INTENSIDAD_5 },
  { 100, 0 },
  { 160, INTENSIDAD_5 },
  { 100, 0 },
  { 160, INTENSIDAD_5 },
};

const PasoVibracion pasosRojo[] = {
  { 600, INTENSIDAD_5 },
};

const PasoVibracion pasosAmarillo[] = {
  { 100, INTENSIDAD_4 },
  { 120, 0 },
  { 100, INTENSIDAD_4 },
  { 120, 0 },
  { 100, INTENSIDAD_4 },
};

const PasoVibracion pasosVerde[] = {
  { 100, INTENSIDAD_3 },
  { 160, 0 },
  { 400, INTENSIDAD_3 },
};

const PasoVibracion pasosCaminando[] = {
  { 70, INTENSIDAD_2 },
};

// La pausa comienza después del último paso. Por ejemplo, el patrón de
// CAMINANDO dura 70 ms y descansa 7930 ms: un toque cada 8 segundos.
const PatronVibracion patrones[] = {
  /* SIN_SEMAFORO */ { pasosSinSemaforo, 3, 3550 },
  /* SIN_SENAL    */ { pasosSinSenal, 7, 2060 },
  /* ROJO         */ { pasosRojo, 1, 1400 },
  /* AMARILLO     */ { pasosAmarillo, 5, 1460 },
  /* VERDE        */ { pasosVerde, 3, 1340 },
  /* CAMINANDO    */ { pasosCaminando, 1, 7930 },
};

EstadoSemaforo estadoActual = SIN_SEMAFORO;
uint8_t indicePasoActual = 0;
bool esperandoSiguientePatron = false;
unsigned long tiempoInicioPaso = 0;

// =============================================================
// 3) SENSOR (BNO055)
// =============================================================

void sensorSetup() {
  if (!bno.begin()) {
    Serial.println("No se encontró el BNO055");
    while (!bno.begin()) {
      Serial.println(".");
      delay(TIEMPO_ESPERA_SETUP);
    }
  }
  bno.setExtCrystalUse(true);
  Serial.println("BNO055 iniciado.");
}

int sensorLeerHeading() {
  imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
  int heading = round(euler.x());
  if (heading < 0) heading += 360;
  return heading;
}

void sensorLeerMagnetometro(float& x, float& y, float& z) {
  imu::Vector<3> mag = bno.getVector(Adafruit_BNO055::VECTOR_MAGNETOMETER);
  x = mag.x();
  y = mag.y();
  z = mag.z();
}

// =============================================================
// 4) BATERÍA
// =============================================================

uint32_t bateriaLeerMilivoltios() {
  uint32_t sumaMilivoltiosPin = 0;

  for (int muestra = 0; muestra < CANTIDAD_MUESTRAS_BATERIA; muestra++) {
    sumaMilivoltiosPin += analogReadMilliVolts(PIN_BATERIA);
    delayMicroseconds(200);
  }

  const float promedioMilivoltiosPin =
    (float)sumaMilivoltiosPin / CANTIDAD_MUESTRAS_BATERIA;
  const float factorDivisor =
    (RESISTENCIA_BATERIA_A_PIN_OHMS + RESISTENCIA_PIN_A_GND_OHMS)
    / RESISTENCIA_PIN_A_GND_OHMS;

  return (uint32_t)roundf(promedioMilivoltiosPin * factorDivisor);
}

uint8_t bateriaCalcularPorcentaje(uint32_t bateriaMilivoltios) {
  const float bateriaVoltios = bateriaMilivoltios / 1000.0f;
  float porcentaje =
    (bateriaVoltios - VOLTAJE_BATERIA_VACIA)
    / (VOLTAJE_BATERIA_LLENA - VOLTAJE_BATERIA_VACIA)
    * 100.0f;

  if (porcentaje < 0.0f) porcentaje = 0.0f;
  if (porcentaje > 100.0f) porcentaje = 100.0f;
  return (uint8_t)roundf(porcentaje);
}

void bateriaPublicarNivel(uint8_t porcentaje) {
  if (caracteristicaBateria == nullptr) return;

  caracteristicaBateria->setValue(&porcentaje, sizeof(porcentaje));
  if (telefonoConectado) {
    caracteristicaBateria->notify();
  }
}

void bateriaSetup() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATERIA, ADC_2_5db);

  const uint32_t bateriaMilivoltios = bateriaLeerMilivoltios();
  porcentajeBateriaActual = bateriaCalcularPorcentaje(bateriaMilivoltios);
  ultimaMedicionBateria = millis();

  Serial.printf(
    "Batería iniciada: %.3f V (%u%%)\n",
    bateriaMilivoltios / 1000.0f,
    porcentajeBateriaActual);
}

void bateriaActualizar() {
  const unsigned long ahora = millis();
  if (ahora - ultimaMedicionBateria < INTERVALO_MEDICION_BATERIA) return;

  // El motor puede bajar momentáneamente el voltaje medido. Esperamos una
  // pausa del patrón para obtener una estimación más estable.
  if (vibracionEstaActiva()) return;

  ultimaMedicionBateria = ahora;
  const uint32_t bateriaMilivoltios = bateriaLeerMilivoltios();
  const uint8_t nuevoPorcentaje =
    bateriaCalcularPorcentaje(bateriaMilivoltios);

  Serial.printf(
    "Batería: %.3f V (%u%%)\n",
    bateriaMilivoltios / 1000.0f,
    nuevoPorcentaje);

  if (nuevoPorcentaje == porcentajeBateriaActual) return;
  porcentajeBateriaActual = nuevoPorcentaje;
  bateriaPublicarNivel(porcentajeBateriaActual);
}

// =============================================================
// 5) BLE
// =============================================================

class EventosServidor : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* servidor, NimBLEConnInfo& informacionConexion) override {
    telefonoConectado = true;
    Serial.printf("Teléfono conectado: %s\n", informacionConexion.getAddress().toString().c_str());
  }

  void onDisconnect(NimBLEServer* servidor, NimBLEConnInfo& informacionConexion, int motivo) override {
    telefonoConectado = false;
    notificacionesHabilitadas = false;
    Serial.printf("Teléfono desconectado. Motivo: %d\n", motivo);
  }
};

class EventosMensaje : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* caracteristica, NimBLEConnInfo& informacionConexion, uint16_t valorSuscripcion) override {
    notificacionesHabilitadas = (valorSuscripcion & 0x01) != 0;
    Serial.println(notificacionesHabilitadas ? "Notificaciones habilitadas." : "Notificaciones deshabilitadas.");
  }
};

class EventosSemaforo : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* caracteristica, NimBLEConnInfo& informacionConexion) override {
    std::string comando = caracteristica->getValue();
    EstadoSemaforo nuevoEstado;

    Serial.print("Comando BLE recibido: ");
    Serial.println(comando.c_str());

    if (interpretarComando(comando.c_str(), nuevoEstado)) {
      vibracionSetEstado(nuevoEstado);
    } else {
      Serial.println("Comando BLE no reconocido.");
    }
  }
};

EventosServidor eventosServidor;
EventosMensaje eventosMensaje;
EventosSemaforo eventosSemaforo;

void bleSetup() {
  NimBLEDevice::init(BLE_NOMBRE);

  servidor = NimBLEDevice::createServer();
  servidor->setCallbacks(&eventosServidor);
  servidor->advertiseOnDisconnect(true);

  NimBLEService* servicio = servidor->createService(BLE_UUID_SERVICIO);

  caracteristicaMensaje = servicio->createCharacteristic(
    BLE_UUID_CARACTERISTICA,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  caracteristicaMensaje->setCallbacks(&eventosMensaje);

  caracteristicaSemaforo = servicio->createCharacteristic(
    BLE_UUID_CARACTERISTICA_SEMAFORO,
    NIMBLE_PROPERTY::WRITE);
  caracteristicaSemaforo->setCallbacks(&eventosSemaforo);

  servicio->start();

  // Servicio estándar BLE Battery Service. Battery Level es un byte de 0 a
  // 100, por lo que otras aplicaciones BLE también pueden interpretarlo.
  NimBLEService* servicioBateria =
    servidor->createService(BLE_UUID_SERVICIO_BATERIA);
  caracteristicaBateria = servicioBateria->createCharacteristic(
    BLE_UUID_NIVEL_BATERIA,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  caracteristicaBateria->setValue(
    &porcentajeBateriaActual,
    sizeof(porcentajeBateriaActual));
  servicioBateria->start();

  NimBLEAdvertising* anuncio = NimBLEDevice::getAdvertising();
  anuncio->setName(BLE_NOMBRE);
  anuncio->addServiceUUID(servicio->getUUID());
  anuncio->start();

  Serial.println("BLE iniciado.");
}

bool bleHayConexion() {
  return telefonoConectado && notificacionesHabilitadas;
}

void bleEnviarMensaje(const char* mensaje) {
  caracteristicaMensaje->setValue(mensaje);
  caracteristicaMensaje->notify();
}

// =============================================================
// 6) VIBRACIÓN (DRV2605L)
// =============================================================

void drvEncender(uint8_t intensidad) {
  drv.setRealtimeValue(intensidad);
}

void drvApagar() {
  drv.setRealtimeValue(0);
}

bool vibracionEstaActiva() {
  if (esperandoSiguientePatron) return false;

  const PatronVibracion& patron = patrones[estadoActual];
  return patron.pasos[indicePasoActual].intensidad > 0;
}

const char* nombreEstadoSemaforo(EstadoSemaforo estado) {
  switch (estado) {
    case SIN_SEMAFORO:
      return "SIN_SEMAFORO";
    case SIN_SENAL:
      return "SIN_SENAL";
    case ROJO:
      return "ROJO";
    case AMARILLO:
      return "AMARILLO";
    case VERDE:
      return "VERDE";
    case CAMINANDO:
      return "CAMINANDO";
  }
  return "DESCONOCIDO";
}

void vibracionIniciarPasoActual() {
  const PatronVibracion& patron = patrones[estadoActual];
  drvEncender(patron.pasos[indicePasoActual].intensidad);
  tiempoInicioPaso = millis();
}

void vibracionSetup() {
  if (!drv.begin()) {
    Serial.println("No se encontró el DRV2605L");
  }

  drv.selectLibrary(1);

  // Motor tipo ERM (motor de moneda/pancake, el más común en pulseras).
  // Si el motor es LRA, cambiar a drv.useLRA() y selectLibrary(6).
  drv.useERM();

  drv.setMode(DRV2605_MODE_REALTIME);  // control directo de amplitud por I2C
  drv.setRealtimeValue(0);

  indicePasoActual = 0;
  esperandoSiguientePatron = false;
  tiempoInicioPaso = millis();

  Serial.println("DRV2605L iniciado.");
}

void vibracionSetEstado(EstadoSemaforo nuevoEstado) {
  estadoActual = nuevoEstado;

  // Cada comando aceptado reinicia el patrón y produce el primer paso
  // inmediatamente. Los siguientes pasos continúan sin bloquear loop().
  drvApagar();
  indicePasoActual = 0;
  esperandoSiguientePatron = false;
  vibracionIniciarPasoActual();

  Serial.printf(
    "Patrón de vibración iniciado: %s\n",
    nombreEstadoSemaforo(estadoActual));
}

void vibracionActualizar() {
  const PatronVibracion& patron = patrones[estadoActual];
  const unsigned long ahora = millis();

  if (esperandoSiguientePatron) {
    if (ahora - tiempoInicioPaso >= patron.pausaEntrePatronesMs) {
      indicePasoActual = 0;
      esperandoSiguientePatron = false;
      vibracionIniciarPasoActual();
    }
    return;
  }

  const PasoVibracion& paso = patron.pasos[indicePasoActual];
  if (ahora - tiempoInicioPaso < paso.duracionMs) {
    return;
  }

  indicePasoActual++;
  if (indicePasoActual < patron.cantidadPasos) {
    vibracionIniciarPasoActual();
  } else {
    drvApagar();
    esperandoSiguientePatron = true;
    tiempoInicioPaso = ahora;
  }
}

// Traduce el texto recibido (ej: "ROJO") al EstadoSemaforo correspondiente.
// Devuelve false si el comando no se reconoce.
bool interpretarComando(const char* comando, EstadoSemaforo& resultado) {
  if (strcmp(comando, "SIN_SEMAFORO") == 0) {
    resultado = SIN_SEMAFORO;
    return true;
  }
  if (strcmp(comando, "SIN_SENAL") == 0) {
    resultado = SIN_SENAL;
    return true;
  }
  if (strcmp(comando, "ROJO") == 0) {
    resultado = ROJO;
    return true;
  }
  if (strcmp(comando, "AMARILLO") == 0) {
    resultado = AMARILLO;
    return true;
  }
  if (strcmp(comando, "VERDE") == 0) {
    resultado = VERDE;
    return true;
  }
  if (strcmp(comando, "CAMINANDO") == 0) {
    resultado = CAMINANDO;
    return true;
  }
  return false;
}

// =============================================================
// 7) SETUP / LOOP
// =============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\nInicio");
  delay(1000);

  Wire.begin(PIN_SDA, PIN_SCL);  // un solo bus I2C para BNO055 + DRV2605L

  sensorSetup();
  bateriaSetup();
  vibracionSetup();
  bleSetup();

  Serial.println("Listo.");
}

void loop() {
  vibracionActualizar();  // avanza la máquina de estados de la vibración
  bateriaActualizar();

  if (bleHayConexion() && millis() - ultimoEnvioBLE >= INTERVALO_ENVIO_BLE) {
    ultimoEnvioBLE = millis();

    int heading = sensorLeerHeading();
    float x, y, z;
    sensorLeerMagnetometro(x, y, z);

    char mensajeBLE[150];
    sprintf(mensajeBLE, "N:%d X:%.2f Y:%.2f Z:%.2f", heading, x, y, z);

    bleEnviarMensaje(mensajeBLE);
    Serial.println(mensajeBLE);
  }
}

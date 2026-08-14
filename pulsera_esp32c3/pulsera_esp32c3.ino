/*
  Pulsera háptica ESP32-C3 — todo en un archivo, modularizado por funciones
  ---------------------------------------------------------------------------
  - Lee orientación/magnetómetro del BNO055 y lo manda por BLE al teléfono.
  - Recibe por WiFi (UDP) el estado del semáforo peatonal.
  - Traduce ese estado en un patrón de vibración en el DRV2605L.

  Estructura del archivo:
    1) Defines (pines, UUIDs, tiempos, intensidades)
    2) Objetos y variables globales
    3) Funciones del sensor (BNO055)
    4) Funciones de BLE
    5) Funciones de vibración (DRV2605L)
    6) Funciones de WiFi
    7) setup() / loop()
*/

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <Adafruit_DRV2605.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <math.h>

// =============================================================
// 1) DEFINES
// =============================================================

// ---- Pines I2C (bus compartido: BNO055 + DRV2605L) ----
#define PIN_SDA 21
#define PIN_SCL 20

// ---- WiFi ----
#define WIFI_SSID "Tinoo"
#define WIFI_PASSWORD "fldsmdfr"
#define PUERTO_UDP 4210
#define TIMEOUT_WIFI_MS 15000

// ---- BLE ----
#define BLE_NOMBRE "ESP32C3_Santino"
#define BLE_UUID_SERVICIO "12345678-1234-1234-1234-123456789abc"
#define BLE_UUID_CARACTERISTICA "abcd1234-1234-1234-1234-abcdef123456"
#define BLE_UUID_CARACTERISTICA_SEMAFORO "abcd1234-1234-1234-1234-abcdef123457"

// ---- Tiempos generales ----
#define TIEMPO_ESPERA_SETUP 1000  // reintento de sensores no encontrados
#define INTERVALO_ENVIO_BLE 50    // 50ms = 20 datos/seg

// ---- Vibración: intensidades para modo Real-Time Playback (0-127) ----
#define INTENSIDAD_2 51   // 2/5
#define INTENSIDAD_3 76   // 3/5
#define INTENSIDAD_4 102  // 4/5
#define INTENSIDAD_5 127  // 5/5

// ---- Vibración: duración de cada pulso (ms) ----
#define PULSO_CORTO 90
#define PULSO_MEDIANO 180
#define PULSO_LARGO 350

// ---- Vibración: separación entre los 2 toques de un patrón doble (ms) ----
#define GAP_DOBLE_RAPIDO 60     // Amarillo: toques pegados
#define GAP_DOBLE_REPETIDO 130  // Sin señal: toques más separados

// =============================================================
// 2) OBJETOS Y VARIABLES GLOBALES
// =============================================================

// ---- Sensor ----
Adafruit_BNO055 bno(55, 0x29);  // 55 = id interno ; 0x29 = dirección I2C

// ---- BLE ----
NimBLEServer* servidor = nullptr;
NimBLECharacteristic* caracteristicaMensaje = nullptr;
NimBLECharacteristic* caracteristicaSemaforo = nullptr;
volatile bool telefonoConectado = false;
volatile bool notificacionesHabilitadas = false;
unsigned long ultimoEnvioBLE = 0;

// ---- Vibración ----
Adafruit_DRV2605 drv;

enum EstadoSemaforo { SIN_SEMAFORO,
                      SIN_SENAL,
                      ROJO,
                      AMARILLO,
                      VERDE };

bool interpretarComando(const char* comando, EstadoSemaforo& resultado);
void vibracionSetEstado(EstadoSemaforo nuevoEstado);

struct PatronVibracion {
  unsigned int bpm;
  unsigned long duracionPulso;
  bool esDoble;
  unsigned long gapDoble;
  uint8_t intensidad;
};

// Un patrón por cada EstadoSemaforo, en el mismo orden del enum.
PatronVibracion patrones[5] = {
  /* SIN_SEMAFORO */ { 65, PULSO_CORTO, false, 0, INTENSIDAD_2 },
  /* SIN_SENAL    */ { 65, PULSO_CORTO, true, GAP_DOBLE_REPETIDO, INTENSIDAD_3 },
  /* ROJO         */ { 60, PULSO_LARGO, false, 0, INTENSIDAD_5 },
  /* AMARILLO     */ { 40, PULSO_CORTO, true, GAP_DOBLE_RAPIDO, INTENSIDAD_4 },
  /* VERDE        */ { 40, PULSO_MEDIANO, false, 0, INTENSIDAD_3 }
};

enum FaseVibracion { ESPERANDO_BEAT,
                     PULSO_1,
                     GAP,
                     PULSO_2 };
FaseVibracion fase = ESPERANDO_BEAT;
EstadoSemaforo estadoActual = SIN_SEMAFORO;
unsigned long tiempoInicioBeat = 0;
unsigned long tiempoInicioFase = 0;

// ---- WiFi ----
WiFiUDP udp;
char paqueteEntrante[64];

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
// 4) BLE
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
// 5) VIBRACIÓN (DRV2605L)
// =============================================================

void drvEncender(uint8_t intensidad) {
  drv.setRealtimeValue(intensidad);
}

void drvApagar() {
  drv.setRealtimeValue(0);
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

  tiempoInicioBeat = millis();
  fase = ESPERANDO_BEAT;

  Serial.println("DRV2605L iniciado.");
}

void vibracionSetEstado(EstadoSemaforo nuevoEstado) {
  if (nuevoEstado == estadoActual) return;

  estadoActual = nuevoEstado;
  // Reinicia el ciclo para que el cambio de patrón se sienta enseguida,
  // en vez de esperar a que termine el beat anterior.
  drvApagar();
  fase = ESPERANDO_BEAT;
  tiempoInicioBeat = millis();
}

void vibracionActualizar() {
  const PatronVibracion& p = patrones[estadoActual];
  unsigned long periodoBeat = 60000UL / p.bpm;
  unsigned long ahora = millis();

  switch (fase) {
    case ESPERANDO_BEAT:
      if (ahora - tiempoInicioBeat >= periodoBeat) {
        tiempoInicioBeat = ahora;
        tiempoInicioFase = ahora;
        drvEncender(p.intensidad);
        fase = PULSO_1;
      }
      break;

    case PULSO_1:
      if (ahora - tiempoInicioFase >= p.duracionPulso) {
        drvApagar();
        tiempoInicioFase = ahora;
        fase = p.esDoble ? GAP : ESPERANDO_BEAT;
      }
      break;

    case GAP:
      if (ahora - tiempoInicioFase >= p.gapDoble) {
        drvEncender(p.intensidad);
        tiempoInicioFase = ahora;
        fase = PULSO_2;
      }
      break;

    case PULSO_2:
      if (ahora - tiempoInicioFase >= p.duracionPulso) {
        drvApagar();
        fase = ESPERANDO_BEAT;
      }
      break;
  }
}

// =============================================================
// 6) WIFI
// =============================================================

void wifiSetup() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Conectando a WiFi");
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < TIMEOUT_WIFI_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi conectado. IP: ");
    Serial.println(WiFi.localIP());
    udp.begin(PUERTO_UDP);
    Serial.printf("Escuchando UDP en puerto %u\n", PUERTO_UDP);
  } else {
    Serial.println("No se pudo conectar al WiFi (se continúa sin él).");
  }
}

// Traduce el texto recibido (ej: "ROJO") al EstadoSemaforo correspondiente.
// Devuelve false si el comando no se reconoce.
bool interpretarComando(const char* comando, EstadoSemaforo& resultado) {
  if (strcmp(comando, "SIN_SEMAFORO") == 0) {
    resultado = SIN_SEMAFORO;
    return true;
  }
  if (strcmp(comando, "SIN_SEÑAL") == 0) {
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
  return false;
}

void wifiActualizar() {
  int tamanioPaquete = udp.parsePacket();
  if (tamanioPaquete <= 0) return;

  int leidos = udp.read(paqueteEntrante, sizeof(paqueteEntrante) - 1);
  paqueteEntrante[leidos] = '\0';

  Serial.print("UDP recibido: ");
  Serial.println(paqueteEntrante);

  EstadoSemaforo nuevoEstado;
  if (interpretarComando(paqueteEntrante, nuevoEstado)) {
    vibracionSetEstado(nuevoEstado);
  } else {
    Serial.println("Comando UDP no reconocido.");
  }
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
  vibracionSetup();
  bleSetup();
  wifiSetup();

  Serial.println("Listo.");
}

void loop() {
  wifiActualizar();       // ¿llegó un nuevo estado de semáforo por WiFi?
  vibracionActualizar();  // avanza la máquina de estados de la vibración

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

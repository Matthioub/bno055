#include <Wire.h>  //comunicación I2C
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <math.h>
#include <NimBLEDevice.h>

Adafruit_BNO055 bno(55, 0x29);  //creación del objeto. 55 = id interno ; 0x29 = dirección del I2C

NimBLEServer* servidor = nullptr;
NimBLECharacteristic* mensaje = nullptr;

volatile bool telefonoConectado = false;
volatile bool notificacionesHabilitadas = false;

#define TIEMPO_ESPERA_DATOS 50    //50ms; 20 datos por segundo
#define TIEMPO_ESPERA_SETUP 1000  //1s

unsigned long tiempoAnteriorDatos = 0;
unsigned long tiempoAnteriorSetUp = 0;

class EventosServidor : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* servidor, NimBLEConnInfo& informacionConexion) override {
    telefonoConectado = true;
    Serial.printf(
      "Teléfono conectado: %s\n",
      informacionConexion.getAddress().toString().c_str());
  }

  void onDisconnect(
    NimBLEServer* servidor,
    NimBLEConnInfo& informacionConexion,
    int motivo) override {
    telefonoConectado = false;
    notificacionesHabilitadas = false;
    Serial.printf("Teléfono desconectado. Motivo: %d\n", motivo);
  }
};

class EventosMensaje : public NimBLECharacteristicCallbacks {
  void onSubscribe(
    NimBLECharacteristic* caracteristica,
    NimBLEConnInfo& informacionConexion,
    uint16_t valorSuscripcion) override {
    notificacionesHabilitadas = (valorSuscripcion & 0x01) != 0;
    Serial.println(
      notificacionesHabilitadas
        ? "Notificaciones habilitadas."
        : "Notificaciones deshabilitadas.");
  }
};

EventosServidor eventosServidor;
EventosMensaje eventosMensaje;

void setup() {
  //begin
  Serial.begin(115200);
  Serial.println("\nInicio");
  delay(1000);



  //BLE
  NimBLEDevice::init("ESP32C3_Santino");  //inicializa el dispositivo y su nombre

  servidor = NimBLEDevice::createServer();  //crea el servidor para conectarse
  servidor->setCallbacks(&eventosServidor);
  servidor->advertiseOnDisconnect(true);

  NimBLEService* servicio = servidor->createService("12345678-1234-1234-1234-123456789abc");  //servicio es como una carpeta | el string es el UUID ("identificador")


  //crear características
  mensaje = servicio->createCharacteristic(            //característica dentro de "servicio"
    "abcd1234-1234-1234-1234-abcdef123456",            //UUID
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);  //es del tipo "lectura y notificación"
  mensaje->setCallbacks(&eventosMensaje);

  servicio->start();

  NimBLEAdvertising* anuncio = NimBLEDevice::getAdvertising();  //"Anuncio" --> "Estoy disponible, me puedo conectar, tengo x características"
  anuncio->setName("ESP32C3_Santino");                          //nombre
  anuncio->addServiceUUID(servicio->getUUID());                 //agregamos "servicio"
  anuncio->start();                                             //inicia el anuncio

  //bno
  Wire.begin(21, 20);  // SDA, SCL

  Serial.println("BLE iniciado.");

  //bno
  if (!bno.begin()) {
    Serial.println("No se encontró el BNO055");
    while (!bno.begin()) {
      tiempoAnteriorSetUp = millis();
      Serial.println(".");
      while (millis() - tiempoAnteriorSetUp < TIEMPO_ESPERA_SETUP) {}
    }
  }
  bno.setExtCrystalUse(true);

  Serial.println("Bno iniciado");
}

void loop() {

  // Orientación
  imu::Vector<3> euler =
    bno.getVector(Adafruit_BNO055::VECTOR_EULER);

  int heading = round(euler.x());

  if (heading < 0) {
    heading += 360;
  }

  // Magnetómetro
  imu::Vector<3> mag =
    bno.getVector(Adafruit_BNO055::VECTOR_MAGNETOMETER);

  // Enviar cada segundo
  if (telefonoConectado &&
      notificacionesHabilitadas &&
      millis() - tiempoAnteriorDatos >= TIEMPO_ESPERA_DATOS) {

    tiempoAnteriorDatos = millis();

    char mensajeBLE[150];

    sprintf(
      mensajeBLE,
      "N:%d X:%.2f Y:%.2f Z:%.2f",
      heading,
      mag.x(),
      mag.y(),
      mag.z());

    mensaje->setValue(mensajeBLE);
    mensaje->notify();

    Serial.println(mensajeBLE);
  }
}

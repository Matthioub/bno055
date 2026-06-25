#include <Wire.h>  //comunicación I2C
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <math.h>
#include <NimBLEDevice.h>

Adafruit_BNO055 bno(55, 0x29);  //creación del objeto. 55 = id interno ; 0x29 = dirección del I2C

NimBLECharacteristic* mensaje;  //mensaje es una variable

#define TIEMPO_ESPERA_DATOS 50    //50ms; 20 datos por segundo
#define TIEMPO_ESPERA_SETUP 1000  //1s

unsigned long tiempoAnteriorDatos = 0;
unsigned long tiempoAnteriorSetUp = 0;

void setup() {
  //begin
  Serial.begin(115200);
  Serial.println("\nInicio");
  delay(1000);



  //BLE
  NimBLEDevice::init("ESP32C3_Santino");  //inicializa el dispositivo y su nombre

  NimBLEServer* /* "dirección" */ servidor = NimBLEDevice::createServer();  //crea el servidor para conectarse

  NimBLEService* servicio = servidor->createService("12345678-1234-1234-1234-123456789abc");  //servicio es como una carpeta | el string es el UUID ("identificador")


  //crear características
  mensaje = servicio->createCharacteristic(            //característica dentro de "servicio"
    "abcd1234-1234-1234-1234-abcdef123456",            //UUID
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);  //es del tipo "lectura y notificación"

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



  //prueba
  mensaje->setValue("Funciona");
  mensaje->notify();
  Serial.println("Notificado");
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
  if (millis() - tiempoAnteriorDatos >= TIEMPO_ESPERA_DATOS) {

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

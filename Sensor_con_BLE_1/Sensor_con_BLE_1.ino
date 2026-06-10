#include <Wire.h>  //comunicación I2C
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <math.h>
#include <NimBLEDevice.h>

Adafruit_BNO055 bno(55, 0x29);  //creación del objeto. 55 = id interno ; 0x29 = dirección del I2C

NimBLECharacteristic* mensaje;  //mensaje es una variable

hw_timer_t* timer = NULL;

volatile int milisegundos = 0;
volatile int segundos = 0;

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



  //timer
  timer = timerBegin(1000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, 1000, true, 0);  //1ms

  Serial.println("BLE iniciado.");

  //bno
  if (!bno.begin()) {
    Serial.println("No se encontró el BNO055");
    while (!bno.begin()) {
      segundos = 0;
      Serial.println(".");
      while (segundos < 1) {}
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
  if (segundos >= 1) {

    segundos = 0;

    char mensajeBLE[150];

    sprintf(
      mensajeBLE,
      "N:%d X:%.2f Y:%.2f Z:%.2f",
      heading,
      mag.x(),
      mag.y(),
      mag.z()
    );

    mensaje->setValue(mensajeBLE);
    mensaje->notify();

    Serial.println(mensajeBLE);
  }
}

void ARDUINO_ISR_ATTR onTimer() {
  segundos++;
}

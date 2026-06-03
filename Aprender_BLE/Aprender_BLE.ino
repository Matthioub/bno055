/*
NO
ANDA
LO
DE
WRITE
!!!!!
*/


#include <NimBLEDevice.h>

NimBLECharacteristic* mensaje;  //mensaje es una variable
NimBLECharacteristic* entrada;

hw_timer_t* timer = NULL;
int contador = 0;
volatile int segundos = 0;


class EntradaCallbacks : public NimBLECharacteristicCallbacks {  //tipo de clase EntradaCallbacks de la lista pública de la librería
  void onWrite(NimBLECharacteristic* caracteristica, NimBLEConnInfo& connInfo) override {  //onWrite() se activa solo cuando se escribe en el celu
    std::string valor = caracteristica->getValue();              //valor almacena lo recibido
    Serial.print("Recibido desde el celular: ");
    Serial.println(valor.c_str());
  }
};

void setup() {
  //begin
  Serial.begin(115200);
  delay(1000);

  //BLE
  NimBLEDevice::init("ESP32C3_Santino");  //inicializa el dispositivo y su nombre

  NimBLEServer* /* "dirección" */ servidor = NimBLEDevice::createServer();  //crea el servidor para conectarse

  NimBLEService* servicio = servidor->createService("12345678-1234-1234-1234-123456789abc");  //servicio es como una carpeta | el string es el UUID ("identificador")


  //crear características
  mensaje = servicio->createCharacteristic(            //característica dentro de "servicio"
    "abcd1234-1234-1234-1234-abcdef123456",            //UUID
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);  //es del tipo "lectura y notificación"

  entrada = servicio->createCharacteristic(
    "11111111-2222-3333-4444-555555555555",
    NIMBLE_PROPERTY::WRITE);  //es del tipo "escritira"


  mensaje->setValue("Contador iniciado");  //el mensaje

  servicio->start();

  NimBLEAdvertising* anuncio = NimBLEDevice::getAdvertising();  //"Anuncio" --> "Estoy disponible, me puedo conectar, tengo x características"
  anuncio->setName("ESP32C3_Santino");                          //nombre
  anuncio->addServiceUUID(servicio->getUUID());                 //agregamos "servicio"
  anuncio->start();                                             //inicia el anuncio


  //timer
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, 1000000, true, 0);  //1s

  Serial.println("BLE iniciado.");
  Serial.println("El valor se actualiza cada segundo");
}

void loop() {
  if (segundos >= 1) {
    segundos--;

    contador++;

    char contadorStr[30];
    sprintf(contadorStr, "Contador: %d", contador);

    mensaje->setValue(contadorStr);
    mensaje->notify();

    Serial.println(contadorStr);
  }
}

void ARDUINO_ISR_ATTR onTimer() {
  segundos++;
}
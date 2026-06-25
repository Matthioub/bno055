#include <Wire.h>  //comunicación I2C
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <math.h>

Adafruit_BNO055 bno(55, 0x29);  //creación del objeto. 55 = id interno ; 0x29 = dirección del I2C

#define INTERVALO 50  // 50 ms = 20 datos por segundo
unsigned long ultimaLectura = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\nInicio");

  Wire.begin(6, 7);  //pines

  if (!bno.begin()) {
    Serial.println("No se encontró el BNO055");
    while (!bno.begin()) {
      Serial.println(".");
      delay(1000);
    }
  }

  delay(1000);

  bno.setExtCrystalUse(true);

  Serial.println("BNO055 iniciado");
}

void loop() {
  unsigned long ahora = millis();

  if (ahora - ultimaLectura >= INTERVALO) {
    ultimaLectura = ahora;

    // ===== CALIBRACIÓN (CRÍTICO) =====
    uint8_t sys, gyro, accel, mag;
    bno.getCalibration(&sys, &gyro, &accel, &mag);

    // ===== ORIENTACIÓN FUSIONADA =====
    imu::Vector<3> euler =
      bno.getVector(Adafruit_BNO055::VECTOR_EULER);

    int heading = round(euler.x());  //"round()" redondea los decimales

    if (heading < 0) {
      heading += 360;
    }
    //Hasta acá el código
    //__________________________________________________________
    //


    /*
    |==========|
    |POR SI    |
    |HACE FALTA|
    |TESTEAR   |
    |ALGO:     |
    |==========|
    */

    // Serial.println("===== ORIENTACION (FUSION BNO055) =====");
    // Serial.print("Heading (Norte): ");
    // Serial.print(heading);
    // Serial.println("°");

    // Serial.println("===== CALIBRACION =====");
    // Serial.print("SYS: ");
    // Serial.print(sys);
    // Serial.print(" GYRO: ");
    // Serial.print(gyro);
    // Serial.print(" ACCEL: ");
    // Serial.print(accel);
    // Serial.print(" MAG: ");
    // Serial.println(mag);

    // Serial.println();
  }
}
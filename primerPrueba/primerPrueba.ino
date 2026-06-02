#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <math.h>

Adafruit_BNO055 bno(55, 0x29);

void setup() {
  Serial.begin(115200);

  Wire.begin(6, 7);

  if (!bno.begin()) {
    Serial.println("No se encontro el BNO055");
    while (1);
  }

  delay(1000);

  bno.setExtCrystalUse(true);

  Serial.println("BNO055 iniciado");
}

void loop() {

  // ===== CALIBRACIÓN (CRÍTICO) =====
  uint8_t sys, gyro, accel, mag;
  bno.getCalibration(&sys, &gyro, &accel, &mag);

  // ===== ORIENTACIÓN FUSIONADA =====
  imu::Vector<3> euler =
      bno.getVector(Adafruit_BNO055::VECTOR_EULER);

  float heading = euler.x();  // yaw = rumbo respecto al norte

  if (heading < 0) {
    heading += 360;
  }

  Serial.println("===== ORIENTACION (FUSION BNO055) =====");
  Serial.print("Heading (Norte): ");
  Serial.print(heading);
  Serial.println("°");

  Serial.println("===== CALIBRACION =====");
  Serial.print("SYS: "); Serial.print(sys);
  Serial.print(" GYRO: "); Serial.print(gyro);
  Serial.print(" ACCEL: "); Serial.print(accel);
  Serial.print(" MAG: "); Serial.println(mag);

  Serial.println();

  delay(500);
}
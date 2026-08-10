#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <Adafruit_DRV2605.h>

#define PIN_SDA 21
#define PIN_SCL 20

Adafruit_BNO055 bno(55, 0x29);
Adafruit_DRV2605 drv;

void setup() {

  Serial.begin(115200);
  delay(1000);

  // =========================
  // I2C
  // =========================

  Wire.begin(PIN_SDA, PIN_SCL);

  Serial.println();
  Serial.println("=== INICIO ===");

  // =========================
  // BNO055
  // =========================

  if (!bno.begin()) {
    Serial.println("ERROR: No se encontro el BNO055");

    while (1) {
      delay(100);
    }
  }

  Serial.println("BNO055 OK");

  bno.setExtCrystalUse(true);

  // =========================
  // DRV2605L
  // =========================

  if (!drv.begin()) {
    Serial.println("ERROR: No se encontro el DRV2605L");

    while (1) {
      delay(100);
    }
  }

  Serial.println("DRV2605L OK");

  // Configuración que usabas
  // en el código que funcionaba

  drv.selectLibrary(1);

  // Motor vibrador ERM
  drv.useERM();

  // Control directo de intensidad
  drv.setMode(DRV2605_MODE_REALTIME);

  // Inicialmente apagado
  drv.setRealtimeValue(0);

  Serial.println("DRV2605L configurado.");
  Serial.println();
  Serial.println("=== SISTEMA LISTO ===");
}

void loop() {

  // =========================
  // LEER BNO055
  // =========================

  imu::Vector<3> euler =
      bno.getVector(Adafruit_BNO055::VECTOR_EULER);

  float heading = euler.x();
  float pitch = euler.y();
  float roll = euler.z();

  Serial.print("Heading: ");
  Serial.print(heading);

  Serial.print(" | Pitch: ");
  Serial.print(pitch);

  Serial.print(" | Roll: ");
  Serial.println(roll);


  // =========================
  // PRUEBA DEL VIBRADOR
  // =========================

  // Si el pitch supera 30 grados
  // hacia cualquier dirección:

  if (abs(pitch) > 30) {

    Serial.println(">>> INCLINACION DETECTADA");

    // Intensidad 0-127
    drv.setRealtimeValue(100);

  } else {

    drv.setRealtimeValue(0);
  }

  delay(100);
}
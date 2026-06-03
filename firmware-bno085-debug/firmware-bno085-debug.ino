#include <Adafruit_BNO08x.h>

// Streams quaternion in Adafruit WebSerial 3D viewer format:
// https://adafruit.github.io/Adafruit_WebSerial_3DModelViewer/

Adafruit_BNO08x bno;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("BNO085 quaternion debug — booting");

  if (!bno.begin_I2C()) {
    Serial.println("BNO085 not found — check wiring");
    while (1) delay(100);
  }

  bno.enableReport(SH2_ROTATION_VECTOR);
  Serial.println("Ready");
}

void loop() {
  sh2_SensorValue_t event;
  unsigned long start = millis();
  bool got = false;
  while (millis() - start < 1000) {
    if (bno.getSensorEvent(&event)) {
      if (event.sensorId == SH2_ROTATION_VECTOR) {
        Serial.printf("Quaternion: %.4f, %.4f, %.4f, %.4f\n",
          event.un.rotationVector.real,
          event.un.rotationVector.i,
          event.un.rotationVector.j,
          event.un.rotationVector.k
        );
        got = true;
        break;
      }
    } else {
      delay(5);
    }
  }
  if (!got) Serial.println("No rotation event in 1s — check BNO wiring");
}

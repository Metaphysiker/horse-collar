#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <esp_sleep.h>

#define BNO_INT_PIN 14

Adafruit_BNO08x bno08x;
sh2_SensorValue_t event;

void setup() {
  Serial.begin(115200);
  delay(2000); // Wait for serial monitor to open
  Serial.println("--- Minimal 1Hz BNO085 Test ---");

  Wire.begin();
  if (!bno08x.begin_I2C()) {
    Serial.println("BNO085 not found! Check wiring.");
    while (1) { delay(10); } // Halt if not found
  }
  Serial.println("BNO085 found.");

  // Enable Rotation Vector at 1 second (1,000,000 microseconds)
  if (bno08x.enableReport(SH2_ROTATION_VECTOR, 1000000)) {
    Serial.println("Rotation Vector enabled at 1Hz (1,000,000 us).");
  } else {
    Serial.println("Failed to enable Rotation Vector!");
  }

  // Setup INT pin to wake from light sleep
  pinMode(BNO_INT_PIN, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BNO_INT_PIN, 0); // 0 = LOW level
  
  Serial.println("Setup complete. Entering sleep loop...");
  Serial.flush();
}

void loop() {
  // 1. Restore I2C bus after light sleep
  Wire.begin(); 
  delay(10); // Small delay to let I2C and sensor stabilize

  // 2. Read whatever is in the sensor buffer
  bool gotData = false;
  while (bno08x.getSensorEvent(&event)) {
    if (event.sensorId == SH2_ROTATION_VECTOR) {
      Serial.printf("Quat: i=%+.3f, j=%+.3f, k=%+.3f, real=%+.3f\n", 
                    event.un.rotationVector.i, 
                    event.un.rotationVector.j, 
                    event.un.rotationVector.k, 
                    event.un.rotationVector.real);
      gotData = true;
    }
  }
  
  if (!gotData) {
    Serial.println("Woke up, but no Rotation Vector data in buffer.");
  }

  // 3. Go back to sleep until the INT pin goes LOW again
  Serial.flush(); // CRITICAL: Ensure serial prints finish before sleeping
  esp_light_sleep_start();
  
  // When the INT pin fires, it wakes up and resumes right here.
  // Then loop() ends, and loop() starts over.
}
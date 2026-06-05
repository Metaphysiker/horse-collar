/*
 * BNO085 Cast Horse Detector - Feather Huzzah32
 *
 * Detects when a horse rotates more than ROTATION_THRESHOLD degrees
 * from its reference orientation. Useful for detecting:
 *   - Rolling
 *   - Falling over
 *   - Cast (stuck on back)
 *
 * Flow:
 *   1. Boot → init BNO085, capture reference quaternion
 *   2. Light sleep until next sample (INT fires at 50 Hz)
 *   3. Wake → compute angle from reference
 *   4. If angle > ROTATION_THRESHOLD → ALERT
 *   5. If alert active and quiet for QUIET_MS → clear alert,
 *      update reference quaternion, go back to sleep
 *
 * Wiring (I2C):
 *   BNO085 VIN → 3.3V | GND → GND
 *   SDA → GPIO 23 | SCL → GPIO 22 | INT → GPIO 14
 *   PS0 + PS1 → GND
 *
 * Libraries: Adafruit BNO08x, Adafruit BusIO
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <esp_sleep.h>
#include <math.h>

// ── Tuning ────────────────────────────────────────────────────────────────────
#define ROTATION_THRESHOLD   90.0f  // degrees — trigger angle from reference
#define QUIET_THRESHOLD      20.0f  // degrees — "back to normal" angle
#define QUIET_MS            3000    // ms within QUIET_THRESHOLD before re-arm
#define REF_SETTLE_MS       2000    // ms to wait for stable reference on boot

// ── Pin / address ─────────────────────────────────────────────────────────────
#define INT_PIN      14
#define BNO085_RESET -1
#define BNO085_ADDR  0x4A

// ── Quaternion struct ─────────────────────────────────────────────────────────
struct Quat { float i, j, k, r; };

// ── Globals ───────────────────────────────────────────────────────────────────
Adafruit_BNO08x   bno(BNO085_RESET);
sh2_SensorValue_t sensorValue;

Quat refQuat      = {0, 0, 0, 1};  // reference orientation (identity)
bool alertActive  = false;
uint32_t alertCount = 0;
bool inQuiet = false;
unsigned long quietSince = 0;

// ── Angle between two quaternions (degrees) ───────────────────────────────────
//
// dot product of two unit quaternions = cos(half_angle)
// angle = 2 * acos(|dot|)  — |dot| handles the q == -q double cover
//
float quaternionAngle(Quat a, Quat b) {
  float dot = a.i*b.i + a.j*b.j + a.k*b.k + a.r*b.r;
  dot = fabs(dot);                    // clamp to [0, 1] for acos safety
  dot = dot > 1.0f ? 1.0f : dot;
  return 2.0f * acos(dot) * (180.0f / M_PI);
}

// ── Read one rotation vector sample ──────────────────────────────────────────
bool readQuat(Quat &q, uint32_t timeoutMs) {
  unsigned long t = millis();
  while (millis() - t < timeoutMs) {
    if (bno.getSensorEvent(&sensorValue) &&
        sensorValue.sensorId == SH2_ROTATION_VECTOR) {
      q.i = sensorValue.un.rotationVector.i;
      q.j = sensorValue.un.rotationVector.j;
      q.k = sensorValue.un.rotationVector.k;
      q.r = sensorValue.un.rotationVector.real;
      return true;
    }
    delay(2);
  }
  return false;
}

// ── Capture a stable reference quaternion ────────────────────────────────────
// Averages several samples to avoid capturing a mid-movement frame
void captureReference() {
  Serial.println(F("[REF] Capturing reference orientation..."));

  // Flush old samples first
  unsigned long flush = millis();
  while (millis() - flush < 200) {
    bno.getSensorEvent(&sensorValue);
    delay(5);
  }

  // Average REF_SETTLE_MS worth of samples
  float si = 0, sj = 0, sk = 0, sr = 0;
  int count = 0;
  unsigned long t = millis();
  while (millis() - t < REF_SETTLE_MS) {
    Quat q;
    if (readQuat(q, 50)) {
      si += q.i; sj += q.j; sk += q.k; sr += q.r;
      count++;
    }
  }

  if (count > 0) {
    // Normalise the averaged quaternion
    float n = sqrt(si*si + sj*sj + sk*sk + sr*sr);
    refQuat = { si/n, sj/n, sk/n, sr/n };
    Serial.printf("[REF] Reference set from %d samples: "
                  "i=%+.3f j=%+.3f k=%+.3f r=%+.3f\n",
                  count, refQuat.i, refQuat.j, refQuat.k, refQuat.r);
  } else {
    Serial.println(F("[REF] WARNING: No samples — using identity quaternion"));
  }
}

// ── Light sleep until INT fires ───────────────────────────────────────────────
void lightSleep() {
  esp_sleep_enable_ext0_wakeup((gpio_num_t)INT_PIN, 0);
  Serial.flush();
  esp_light_sleep_start();
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n===================================================="));
  Serial.println(F("  BNO085 Cast Horse Detector - Feather Huzzah32"));
  Serial.println(F("===================================================="));
  Serial.printf( "  Trigger angle : %.0f degrees from reference\n", ROTATION_THRESHOLD);
  Serial.printf( "  Quiet angle   : %.0f degrees (reset zone)\n",   QUIET_THRESHOLD);
  Serial.printf( "  Quiet time    : %d ms before re-arm\n\n",       QUIET_MS);

  pinMode(INT_PIN, INPUT_PULLUP);
  Wire.begin();
  Wire.setClock(400000);

  if (!bno.begin_I2C(BNO085_ADDR)) {
    Serial.println(F("[ERROR] BNO085 not found. Halting."));
    while (true) delay(1000);
  }
  Serial.println(F("[OK] BNO085 initialised"));

  if (!bno.enableReport(SH2_ROTATION_VECTOR, 100000)) {
    Serial.println(F("[ERROR] Could not enable ROTATION_VECTOR. Halting."));
    while (true) delay(1000);
  }
  Serial.println(F("[OK] Rotation vector enabled at 50 Hz"));

  captureReference();
  Serial.println(F("\n[INFO] Monitoring for rotation > 180 degrees...\n"));
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {

  // ── Sleep until next sample ───────────────────────────────────────────────
  lightSleep();

  // ── Read current orientation ──────────────────────────────────────────────
  Quat current;
  if (!readQuat(current, 100)) return;

  float angle = quaternionAngle(refQuat, current);

  // ── Below trigger threshold and no active alert: silent, sleep ────────────
  if (!alertActive && angle < ROTATION_THRESHOLD) return;

  // ── Crossed rotation threshold: new alert ────────────────────────────────
  if (!alertActive && angle >= ROTATION_THRESHOLD) {
    alertActive = true;
    alertCount++;
    inQuiet    = false;
    quietSince = 0;
    Serial.println(F("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"));
    Serial.printf( "  ALERT #%lu — ROTATION %.1f degrees detected!\n",
                   alertCount, angle);
    Serial.println(F("  Horse may be cast, fallen, or rolling!"));
    Serial.println(F("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"));
  }

  // ── Active alert: keep printing angle, watch for recovery ─────────────────
  if (alertActive) {
    Serial.printf("[ALERT] angle=%.1f deg  (trigger=%.0f  quiet=%.0f)\n",
                  angle, ROTATION_THRESHOLD, QUIET_THRESHOLD);

    // Recovery: stay within QUIET_THRESHOLD for QUIET_MS
    if (angle < QUIET_THRESHOLD) {
      if (!inQuiet) {
        inQuiet    = true;
        quietSince = millis();
        Serial.println(F("[INFO] Back near reference — starting quiet timer..."));
      } else if (millis() - quietSince >= QUIET_MS) {
        // Recovered — update reference to current position and re-arm
        alertActive = false;
        inQuiet     = false;
        Serial.println(F("[OK] Rotation back to normal. Updating reference.\n"));
        captureReference();
        Serial.println(F("[INFO] Re-armed. Sleeping.\n"));
      }
    } else {
      // Still rotated — reset quiet timer
      if (inQuiet) {
        inQuiet = false;
        Serial.println(F("[INFO] Still rotated — quiet timer reset"));
      }
    }
  }

  delay(20); // ~50 Hz when awake
}

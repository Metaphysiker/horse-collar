#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <esp_sleep.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

// -------------------- CONFIG --------------------

#define BNO_INT_PIN           14
#define WIFI_TIMEOUT_MS       15000
#define HTTP_TIMEOUT_MS       5000
#define NTP_TIMEOUT_MS        10000
#define HEARTBEAT_INTERVAL_MS 60000

// Hysteresis band:
//   Standing → Lying  when gz drops below LYING_ENTER  (tilt > 45°)
//   Lying → Standing  when gz rises above LYING_EXIT   (tilt < 30°)
// gz ≈ 1.0 when collar is upright, ≈ 0.0 when on its side.
const float LYING_ENTER_DEG  = 75.0f;   // cross this going down  → lying
const float LYING_EXIT_DEG   = 60.0f;   // cross this going up    → standing
const float LYING_ENTER_COS  = cos(LYING_ENTER_DEG * PI / 180.0f);  // ≈ 0.707
const float LYING_EXIT_COS   = cos(LYING_EXIT_DEG  * PI / 180.0f);  // ≈ 0.866

const uint32_t ROTATION_INTERVAL     = 10000;   //  10 ms — BNO085 minimum
const uint32_t ACCEL_INTERVAL        = 100000;  // 100 ms
const uint32_t GYRO_INTERVAL         = 100000;  // 100 ms
const unsigned long PROCESS_INTERVAL_MS = 1000; //   1 s between processing cycles

// -------------------- STRUCTS --------------------

struct Quat {
  float x, y, z, w;
};

struct SensorReadingV2 {
  String horseId;
  String timestamp;

  float qw = 1.0f;
  float qx = 0.0f;
  float qy = 0.0f;
  float qz = 0.0f;

  float acceleration    = 0.0f;
  float angularVelocity = 0.0f;

  String alertReason;
  String readingType;

  void toJson(JsonObject obj) const {
    obj["horseId"]         = horseId;
    obj["timestamp"]       = timestamp;
    obj["qw"]              = qw;
    obj["qx"]              = qx;
    obj["qy"]              = qy;
    obj["qz"]              = qz;
    obj["acceleration"]    = acceleration;
    obj["angularVelocity"] = angularVelocity;
    if (alertReason.length() > 0)
      obj["alertReason"]   = alertReason;
    obj["readingType"]     = readingType;
  }
};

struct SensorReadingV2Dto {
  SensorReadingV2 rawReading;
  SensorReadingV2 normalizedReading;

  String toJson() const {
    JsonDocument doc;
    JsonObject raw = doc["rawReading"].to<JsonObject>();
    rawReading.toJson(raw);
    JsonObject normalized = doc["normalizedReading"].to<JsonObject>();
    normalizedReading.toJson(normalized);
    String output;
    serializeJson(doc, output);
    return output;
  }
};

// -------------------- GLOBALS --------------------

WiFiMulti         wifiMulti;
Adafruit_BNO08x   bno08x;
sh2_SensorValue_t event;

volatile bool newDataReady = false;

RTC_DATA_ATTR bool          isCalibrated          = false;
RTC_DATA_ATTR float         refQx                 = 0.0f;
RTC_DATA_ATTR float         refQy                 = 0.0f;
RTC_DATA_ATTR float         refQz                 = 0.0f;
RTC_DATA_ATTR float         refQw                 = 1.0f;
RTC_DATA_ATTR unsigned long lastHeartbeatMs       = 0;
RTC_DATA_ATTR unsigned long lastProcessedMs       = 0;
RTC_DATA_ATTR int           sendEveryN            = 60;
RTC_DATA_ATTR int           readingsSinceLastSend = 0;
RTC_DATA_ATTR bool          firstBoot             = true;
RTC_DATA_ATTR bool          timeSynced            = false;
RTC_DATA_ATTR bool          everHadAccel          = false;
RTC_DATA_ATTR bool          everHadGyro           = false;

// currentDto is NOT RTC_DATA_ATTR — resets on hard reboot, which is fine.
SensorReadingV2Dto currentDto;

enum Posture { STANDING, LYING };
Posture posture        = STANDING;
bool    postureChanged = false;

static float calAccum[4] = { 0, 0, 0, 0 };
static int   calCount    = 0;
const  int   CAL_SAMPLES = 10;

// -------------------- FORWARD DECLARATIONS --------------------

void            calibrateAccumulate(float qx, float qy, float qz, float qw);
SensorReadingV2 normalize(const SensorReadingV2& raw);
Posture         detectPosture(float gz);
float           magnitude(float x, float y, float z);
Quat            conjugate(const Quat& q);
Quat            multiply(const Quat& a, const Quat& b);

bool   ensureWifi();
void   connectWifi();
void   disconnectWifi();
void   syncTime();
void   fetchConfig();
void   sendPostureChange();
bool   get(const String& url);
bool   post(const String& url, const String& body);
void   IRAM_ATTR bnoISR();
void   sendHeartbeat();
bool   sendReading(const SensorReadingV2Dto& dto);
String isoTimestamp();

// -------------------- SETUP --------------------

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Setup");

  Wire.begin();

  if (!bno08x.begin_I2C()) {
    Serial.println("BNO085 not found — halting");
    while (1) delay(10);
  }

  delay(1000);

  bool ok1 = bno08x.enableReport(SH2_ROTATION_VECTOR,      ROTATION_INTERVAL);
  bool ok2 = bno08x.enableReport(SH2_LINEAR_ACCELERATION,  ACCEL_INTERVAL);
  bool ok3 = bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, GYRO_INTERVAL);
  Serial.printf("Reports enabled — RV=%d ACC=%d GYRO=%d\n", ok1, ok2, ok3);

  pinMode(BNO_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BNO_INT_PIN), bnoISR, FALLING);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BNO_INT_PIN, 0);

  for (auto& n : WIFI_NETWORKS)
    wifiMulti.addAP(n.ssid, n.password);

  if (firstBoot) {
    firstBoot = false;
    connectWifi();
    syncTime();
    fetchConfig();
    disconnectWifi();
  }

  Serial.println("Ready.");
  Serial.flush();
}

// -------------------- ISR --------------------

void IRAM_ATTR bnoISR() {
  newDataReady = true;
}

// -------------------- LOOP --------------------

void loop() {
  bool hasData;
  noInterrupts();
  hasData      = newDataReady;
  newDataReady = false;
  interrupts();

  if (!hasData) {
    esp_light_sleep_start();
    return;
  }

  unsigned long now = millis();

  // Not time to process yet — drain buffer so INT pin resets, then sleep
  if (now - lastProcessedMs < PROCESS_INTERVAL_MS) {
    while (bno08x.getSensorEvent(&event)) {}
    esp_light_sleep_start();
    return;
  }

  lastProcessedMs = now;
  Serial.println("Processing...");

  // Rotation must arrive every cycle; accel and gyro only need to have
  // arrived at least once since boot.
  bool hasRotation = false;

  while (bno08x.getSensorEvent(&event)) {

    if (event.sensorId == SH2_ROTATION_VECTOR) {
      currentDto.rawReading.qx = event.un.rotationVector.i;
      currentDto.rawReading.qy = event.un.rotationVector.j;
      currentDto.rawReading.qz = event.un.rotationVector.k;
      currentDto.rawReading.qw = event.un.rotationVector.real;

      if (!isCalibrated) {
        Serial.printf("Cal sample %d / %d\n", calCount + 1, CAL_SAMPLES);
        calibrateAccumulate(currentDto.rawReading.qx,
                            currentDto.rawReading.qy,
                            currentDto.rawReading.qz,
                            currentDto.rawReading.qw);
      }
      hasRotation = true;
    }

    else if (event.sensorId == SH2_LINEAR_ACCELERATION) {
      currentDto.rawReading.acceleration =
        magnitude(event.un.linearAcceleration.x,
                  event.un.linearAcceleration.y,
                  event.un.linearAcceleration.z);
      everHadAccel = true;
    }

    else if (event.sensorId == SH2_GYROSCOPE_CALIBRATED) {
      currentDto.rawReading.angularVelocity =
        magnitude(event.un.gyroscope.x,
                  event.un.gyroscope.y,
                  event.un.gyroscope.z);
      everHadGyro = true;
    }
  }

  if (!hasRotation || !everHadAccel || !everHadGyro) return;
  if (!isCalibrated) return;

  currentDto.rawReading.readingType = "Raw";
  currentDto.rawReading.horseId     = HORSE_ID;

  currentDto.normalizedReading             = normalize(currentDto.rawReading);
  currentDto.normalizedReading.readingType = "Normalized";
  currentDto.normalizedReading.horseId     = HORSE_ID;

  // gz is the Z component of the normalized gravity vector.
  // ≈ +1.0  collar upright (horse standing)
  // ≈  0.0  collar on its side (horse lying)
  float nqw = currentDto.normalizedReading.qw;
  float nqx = currentDto.normalizedReading.qx;
  float nqy = currentDto.normalizedReading.qy;
  float nqz = currentDto.normalizedReading.qz;
  float gz  = nqw*nqw - nqx*nqx - nqy*nqy + nqz*nqz;

  Serial.printf("gz=%.3f  posture=%s\n", gz,
                posture == LYING ? "LYING" : "STANDING");

  posture = detectPosture(gz);

  bool needsHeartbeat = (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS);

  readingsSinceLastSend++;
  if (readingsSinceLastSend >= sendEveryN || postureChanged || needsHeartbeat) {
    readingsSinceLastSend = 0;
    String ts = isoTimestamp();
    currentDto.rawReading.timestamp        = ts;
    currentDto.normalizedReading.timestamp = ts;
    connectWifi();
    sendReading(currentDto);
    if (postureChanged) { sendPostureChange(); postureChanged = false; }
    if (needsHeartbeat) { lastHeartbeatMs = now; sendHeartbeat(); }
    disconnectWifi();
  }
}

// -------------------- HELPERS --------------------

float magnitude(float x, float y, float z) {
  return sqrt(x*x + y*y + z*z);
}

// -------------------- QUATERNION --------------------

Quat conjugate(const Quat& q) {
  return { -q.x, -q.y, -q.z, q.w };
}

Quat multiply(const Quat& a, const Quat& b) {
  return {
    a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
    a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
  };
}

// -------------------- CALIBRATION --------------------

void calibrateAccumulate(float qx, float qy, float qz, float qw) {
  calAccum[0] += qx;
  calAccum[1] += qy;
  calAccum[2] += qz;
  calAccum[3] += qw;
  calCount++;

  if (calCount < CAL_SAMPLES) return;

  refQx = calAccum[0] / CAL_SAMPLES;
  refQy = calAccum[1] / CAL_SAMPLES;
  refQz = calAccum[2] / CAL_SAMPLES;
  refQw = calAccum[3] / CAL_SAMPLES;

  float len = sqrt(refQx*refQx + refQy*refQy + refQz*refQz + refQw*refQw);
  refQx /= len;
  refQy /= len;
  refQz /= len;
  refQw /= len;

  isCalibrated = true;
  Serial.printf("Calibrated. ref=(%.3f, %.3f, %.3f, %.3f)\n",
                refQx, refQy, refQz, refQw);
}

// -------------------- NORMALIZATION --------------------

SensorReadingV2 normalize(const SensorReadingV2& raw) {
  Quat current   = { raw.qx, raw.qy, raw.qz, raw.qw };
  Quat reference = { refQx,  refQy,  refQz,  refQw  };
  Quat adj       = multiply(conjugate(reference), current);

  SensorReadingV2 out = raw;
  out.qx = adj.x;
  out.qy = adj.y;
  out.qz = adj.z;
  out.qw = adj.w;
  return out;
}

// -------------------- POSTURE DETECTION --------------------
//
// Hysteresis prevents rapid flickering near the threshold:
//
//   STANDING → LYING   when gz < LYING_ENTER_COS  (tilt increases past 45°)
//   LYING → STANDING   when gz > LYING_EXIT_COS   (tilt decreases below 30°)
//
//   gz ≈ 1.0  upright
//   gz ≈ 0.0  on side

Posture detectPosture(float gz) {
  switch (posture) {
    case STANDING:
      if (gz < LYING_ENTER_COS) {
        Serial.println("→ LYING");
        postureChanged = true;
        return LYING;
      }
      Serial.println("  STANDING");
      break;

    case LYING:
      if (gz > LYING_EXIT_COS) {
        Serial.println("→ STANDING");
        //postureChanged = true;
        return STANDING;
      }
      Serial.println("  LYING");
      break;
  }
  return posture;
}

// -------------------- READING --------------------

bool sendReading(const SensorReadingV2Dto& dto) {
  Serial.println("Sending reading...");
  String url = String(SERVER_URL) + "/v2/horses/" + HORSE_ID + "/readings/from-dto";
  return post(url, dto.toJson());
}

// -------------------- POSTURE CHANGE --------------------

void sendPostureChange() {
  String body = "{\"posture\":\"" +
                String(posture == LYING ? "lying" : "standing") +
                "\"}";
  get(String(SERVER_URL) + "/horses/" + HORSE_ID + "/readings/posture-change");
}

// -------------------- WIFI --------------------

void connectWifi() {
  ensureWifi();
}

bool ensureWifi() {
  if (WiFi.getMode() == WIFI_OFF) {
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
  }
  if (wifiMulti.run() == WL_CONNECTED) return true;

  unsigned long start = millis();
  while (millis() - start < WIFI_TIMEOUT_MS) {
    if (wifiMulti.run() == WL_CONNECTED) return true;
    delay(500);
  }
  Serial.println("WiFi timeout");
  return false;
}

void disconnectWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// -------------------- TIME SYNC --------------------

void syncTime() {
  if (!ensureWifi()) return;
  configTime(TZ_OFFSET, 0, NTP_SERVER);
  unsigned long start = millis();
  time_t now = 0;
  while (millis() - start < NTP_TIMEOUT_MS) {
    time(&now);
    if (now > 100000) { timeSynced = true; return; }
    delay(500);
  }
  Serial.println("NTP sync failed");
}

// -------------------- CONFIG --------------------

void fetchConfig() {
  String url = String(SERVER_URL) + "/horses/" + HORSE_ID + "/config";

  HTTPClient http;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  http.begin(secureClient, url);
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (http.GET() == 200) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());

    sendEveryN = doc["sendEveryN"] | sendEveryN;
    refQx      = doc["refQx"]     | refQx;
    refQy      = doc["refQy"]     | refQy;
    refQz      = doc["refQz"]     | refQz;
    refQw      = doc["refQw"]     | refQw;

    // Remote recalibration
    if (doc["recalibrate"] | false) {
      isCalibrated = false;
      calCount     = 0;
      memset(calAccum, 0, sizeof(calAccum));
      Serial.println("Remote recalibration triggered");
    }

    // Remote reboot
    if (doc["reboot"] | false) {
      post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/config/reboot/clear", "{}");
      Serial.println("Remote reboot triggered");
      delay(500);
      ESP.restart();
    }

    // Optionally pull thresholds from server in future:
    // lyingEnterDeg = doc["lyingEnterDeg"] | lyingEnterDeg;
    // lyingExitDeg  = doc["lyingExitDeg"]  | lyingExitDeg;
  }
  http.end();
}

// -------------------- NETWORK --------------------

bool get(const String& url) {
  if (!ensureWifi()) return false;

  HTTPClient http;
  WiFiClientSecure secureClient;
  if (url.startsWith("https")) {
    secureClient.setInsecure();
    http.begin(secureClient, url);
  } else {
    http.begin(url);
  }
  http.setTimeout(HTTP_TIMEOUT_MS);

  int code = http.GET();
  Serial.printf("GET %s → %d\n", url.c_str(), code);

  if (code < 0) {
    Serial.println("GET failed: " + http.errorToString(code));
    http.end();
    return false;
  }
  if (code < 200 || code >= 300) {
    Serial.println("GET non-2xx: " + http.getString());
    http.end();
    return false;
  }
  http.end();
  return true;
}

bool post(const String& url, const String& body) {
  Serial.printf("POST %s\n", url.c_str());
  if (!ensureWifi()) return false;

  HTTPClient http;
  WiFiClientSecure secureClient;
  if (url.startsWith("https")) {
    secureClient.setInsecure();
    http.begin(secureClient, url);
  } else {
    http.begin(url);
  }
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(body);
  Serial.printf("POST → %d\n", code);
  if (code < 0) {
    Serial.println("POST failed: " + http.errorToString(code));
    http.end();
    return false;
  }
  if (code < 200 || code >= 300) {
    Serial.println("POST non-2xx: " + http.getString());
    http.end();
    return false;
  }
  http.end();
  return true;
}

// -------------------- HEARTBEAT --------------------

void sendHeartbeat() {
  get(String(SERVER_URL) + "/horses/" + HORSE_ID + "/heartbeat");
}

// -------------------- TIMESTAMP --------------------

String isoTimestamp() {
  time_t now;
  time(&now);
  struct tm t;
  gmtime_r(&now, &t);
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
  return String(buf);
}
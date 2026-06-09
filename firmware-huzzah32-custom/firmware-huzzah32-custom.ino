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

#define BNO_INT_PIN 14
#define WIFI_TIMEOUT_MS 15000
#define HTTP_TIMEOUT_MS 5000
#define NTP_TIMEOUT_MS 10000
#define HEARTBEAT_INTERVAL_MS 60000

const float LYING_THRESHOLD = 75.0f;
const float STANDING_THRESHOLD = 45.0f;

const uint32_t SENSOR_INTERVAL = 10000;  // microseconds

// -------------------- WIFI --------------------

WiFiMulti wifiMulti;

// -------------------- QUATERNION --------------------

struct Quaternion {
  float x, y, z, w;
};

Quaternion conjugate(const Quaternion& q) {
  return { -q.x, -q.y, -q.z, q.w };
}

Quaternion multiply(const Quaternion& a, const Quaternion& b) {
  return {
    a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
    a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
  };
}

// -------------------- SensorReadingV2 --------------------

struct SensorReadingV2 {
    String horseId;
    String timestamp;

    float qw = 1.0f;
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;

    float acceleration = 0.0f;
    float angularVelocity = 0.0f;

    String alertReason;
    String readingType;

    void toJson(JsonObject obj) const {
        obj["horseId"] = HORSE_ID;
        obj["timestamp"] = timestamp;

        obj["qw"] = qw;
        obj["qx"] = qx;
        obj["qy"] = qy;
        obj["qz"] = qz;

        obj["acceleration"] = acceleration;
        obj["angularVelocity"] = angularVelocity;

        if (alertReason.length() > 0)
            obj["alertReason"] = alertReason;

        obj["readingType"] = readingType;
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

// -------------------- SENSOR READING --------------------

class SensorReading {
public:
  unsigned long timestamp;
  float qx, qy, qz, qw;
  float acceleration;
  float angularVelocity;
  float accuracy;

  SensorReading()
    : timestamp(0), qx(0), qy(0), qz(0), qw(1),
      acceleration(0), angularVelocity(0), accuracy(0) {}

  SensorReading(unsigned long ts,
                float x, float y, float z, float w,
                float accel, float angVel, float acc)
    : timestamp(ts), qx(x), qy(y), qz(z), qw(w),
      acceleration(accel), angularVelocity(angVel), accuracy(acc) {}

  String toJson() const {
    DynamicJsonDocument doc(256);
    doc["timestamp"] = timestamp;
    doc["qx"] = qx;
    doc["qy"] = qy;
    doc["qz"] = qz;
    doc["qw"] = qw;
    doc["acceleration"] = acceleration;
    doc["angularVelocity"] = angularVelocity;
    doc["accuracy"] = accuracy;
    String out;
    serializeJson(doc, out);
    return out;
  }
};

// -------------------- GLOBALS --------------------

Adafruit_BNO08x bno08x;
sh2_SensorValue_t event;

volatile bool newDataReady = false;

RTC_DATA_ATTR bool isCalibrated = false;

RTC_DATA_ATTR float refQx = 0.0f;
RTC_DATA_ATTR float refQy = 0.0f;
RTC_DATA_ATTR float refQz = 0.0f;
RTC_DATA_ATTR float refQw = 1.0f;

RTC_DATA_ATTR unsigned long lastHeartbeatMs = 0;


SensorReading currentReading;
SensorReadingV2Dto currentSensorReadingV2Dto;


enum Posture { STANDING,
               LYING };
Posture posture = STANDING;

bool postureChanged = false;

RTC_DATA_ATTR int sendEveryN = 60;
RTC_DATA_ATTR bool firstBoot = true;
RTC_DATA_ATTR bool timeSynced = false;

// -------------------- FORWARD DECLARATIONS --------------------

void calibrate(const SensorReading& r);
SensorReading getAdjustedReading(const SensorReading& r);
Posture detectPosture(float gz);

bool ensureWifi();
void connectWifi();
void disconnectWifi();
void syncTime();
void fetchConfig();
void sendPostureChange();
bool get(const String& url);
bool post(const String& url, const String& body);
void IRAM_ATTR bnoISR();
void sendHeartbeat();
bool sendReading(const SensorReadingV2Dto& dto);
String isoTimestamp();


// -------------------- SETUP --------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.print("Booting... ");


  Wire.begin();

  if (!bno08x.begin_I2C()) {
    Serial.println("BNO085 not found");
    while (1) delay(10);
  }

  delay(1000);  // give sensor time to settle


  Serial.println("Enabling reports...");
  bool ok1 = bno08x.enableReport(SH2_ROTATION_VECTOR, SENSOR_INTERVAL);
  bool ok2 = bno08x.enableReport(SH2_LINEAR_ACCELERATION, SENSOR_INTERVAL);
  bool ok3 = bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, SENSOR_INTERVAL);

  Serial.printf("RV=%d ACC=%d GYRO=%d\n", ok1, ok2, ok3);

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
}

// -------------------- ISR --------------------

void IRAM_ATTR bnoISR() {
  newDataReady = true;
}

// -------------------- LOOP --------------------

void loop() {
  currentSensorReadingV2Dto.rawReading = SensorReadingV2();
  currentSensorReadingV2Dto.normalizedReading = SensorReadingV2();
  String ts = isoTimestamp();
  currentSensorReadingV2Dto.rawReading.timestamp = ts;
  currentSensorReadingV2Dto.normalizedReading.timestamp = ts;

  Serial.printf("sensorId=%d\n", event.sensorId);
  if (postureChanged) {
    Serial.println("Posture changed — sending update");
    postureChanged = false;
    connectWifi();
    sendPostureChange();
    disconnectWifi();
  }

  // Heartbeat
  unsigned long now = millis();
  if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatMs = now;
    connectWifi();
    //sendHeartbeat();
    disconnectWifi();
  }

  bool hasData;
  noInterrupts();
  hasData = newDataReady;
  newDataReady = false;
  interrupts();

  if (!hasData) {
    Serial.println("No new data — sleeping");
    esp_light_sleep_start();
    return;
  }

  Serial.println("New sensor data ready — reading...");


  while (bno08x.getSensorEvent(&event)) {
    Serial.printf("EVENT: id=%d\n", event.sensorId);

    if (event.sensorId == SH2_ROTATION_VECTOR) {

      currentSensorReadingV2Dto.rawReading.readingType = "Raw";
      currentSensorReadingV2Dto.rawReading.horseId = HORSE_ID;
      currentSensorReadingV2Dto.rawReading.timestamp = isoTimestamp();
      currentSensorReadingV2Dto.rawReading.qx = event.un.rotationVector.i;
      currentSensorReadingV2Dto.rawReading.qy = event.un.rotationVector.j;
      currentSensorReadingV2Dto.rawReading.qz = event.un.rotationVector.k;
      currentSensorReadingV2Dto.rawReading.qw = event.un.rotationVector.real;


      currentReading.qx = event.un.rotationVector.i;
      currentReading.qy = event.un.rotationVector.j;
      currentReading.qz = event.un.rotationVector.k;
      currentReading.qw = event.un.rotationVector.real;
      currentReading.timestamp = millis();

      if (!isCalibrated) {
        calibrate(currentReading);
        continue;
      }
    }

    else if (event.sensorId == SH2_LINEAR_ACCELERATION) {
      currentSensorReadingV2Dto.rawReading.acceleration =
        sqrt(
          event.un.linearAcceleration.x * event.un.linearAcceleration.x + event.un.linearAcceleration.y * event.un.linearAcceleration.y + event.un.linearAcceleration.z * event.un.linearAcceleration.z);
      currentReading.acceleration =
        sqrt(
          event.un.linearAcceleration.x * event.un.linearAcceleration.x + event.un.linearAcceleration.y * event.un.linearAcceleration.y + event.un.linearAcceleration.z * event.un.linearAcceleration.z);
    }

    else if (event.sensorId == SH2_GYROSCOPE_CALIBRATED) {

      currentSensorReadingV2Dto.rawReading.angularVelocity =
        sqrt(
          event.un.gyroscope.x * event.un.gyroscope.x + event.un.gyroscope.y * event.un.gyroscope.y + event.un.gyroscope.z * event.un.gyroscope.z);
      currentReading.angularVelocity =
        sqrt(
          event.un.gyroscope.x * event.un.gyroscope.x + event.un.gyroscope.y * event.un.gyroscope.y + event.un.gyroscope.z * event.un.gyroscope.z);
    }

      SensorReading adjusted = getAdjustedReading(currentReading);

      currentSensorReadingV2Dto.normalizedReading.readingType = "Normalized";
      currentSensorReadingV2Dto.normalizedReading.horseId = HORSE_ID;
      currentSensorReadingV2Dto.normalizedReading.timestamp = isoTimestamp();
      currentSensorReadingV2Dto.normalizedReading.qx = adjusted.qx;
      currentSensorReadingV2Dto.normalizedReading.qy = adjusted.qy;
      currentSensorReadingV2Dto.normalizedReading.qz = adjusted.qz;
      currentSensorReadingV2Dto.normalizedReading.qw = adjusted.qw;
      currentSensorReadingV2Dto.normalizedReading.acceleration = adjusted.acceleration;
      currentSensorReadingV2Dto.normalizedReading.angularVelocity = adjusted.angularVelocity;

      Serial.println("Before sending:");
      sendReading(currentSensorReadingV2Dto);

      float qw = adjusted.qw;
      float qx = adjusted.qx;
      float qy = adjusted.qy;
      float qz = adjusted.qz;

      float gz = qw * qw
                 - qx * qx
                 - qy * qy
                 + qz * qz;

      posture = detectPosture(gz);

  }
}

// -------------------- CALIBRATION --------------------

void calibrate(const SensorReading& r) {
  refQx = r.qx;
  refQy = r.qy;
  refQz = r.qz;
  refQw = r.qw;

  isCalibrated = true;

  Serial.printf(
    "Calibrated ref=(%.3f, %.3f, %.3f, %.3f)\n",
    refQx, refQy, refQz, refQw);
}

// -------------------- SENSOR ADJUSTMENT --------------------

SensorReading getAdjustedReading(const SensorReading& r) {
  if (!isCalibrated) return r;

  Quaternion current = { r.qx, r.qy, r.qz, r.qw };
  Quaternion reference = {
    refQx, refQy, refQz, refQw
  };

  Quaternion adj = multiply(conjugate(reference), current);

  return SensorReading(r.timestamp,
                       adj.x, adj.y, adj.z, adj.w,
                       r.acceleration, r.angularVelocity, r.accuracy);
}

// -------------------- POSTURE DETECTION --------------------

Posture detectPosture(float gz) {
  switch (posture) {
    case STANDING:
      if (gz < cos(LYING_THRESHOLD * PI / 180.0f)) {
        postureChanged = true;
        return LYING;
      }
      break;
    case LYING:
      if (gz > cos(STANDING_THRESHOLD * PI / 180.0f)) {
        postureChanged = true;
        return STANDING;
      }
      break;
  }
  return posture;
}

bool sendReading(const SensorReadingV2Dto& dto) {
  String url = String(SERVER_URL) + "/v2/horses/" + HORSE_ID + "/readings/from-dto";
  Serial.println("Sending reading to " + url);
  return post(url, dto.toJson());
}

// -------------------- WIFI --------------------

void connectWifi() {
  ensureWifi();
}

bool ensureWifi() {
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  if (wifiMulti.run() == WL_CONNECTED) return true;

  unsigned long start = millis();
  while (millis() - start < WIFI_TIMEOUT_MS) {
    if (wifiMulti.run() == WL_CONNECTED) {
      return true;
    }
    delay(500);
  }
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
    if (now > 100000) {
      timeSynced = true;
      return;
    }
    delay(500);
  }
  Serial.println(" failed");
}

// -------------------- CONFIG --------------------

void fetchConfig() {
  String url = String(SERVER_URL) + "/horses/" + HORSE_ID + "/config";
  if (!get(url)) return;

  HTTPClient http;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  http.begin(secureClient, url);
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (http.GET() == 200) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());

    sendEveryN = doc["sendEveryN"] | sendEveryN;
    refQx = doc["refQx"] | refQx;
    refQy = doc["refQy"] | refQy;
    refQz = doc["refQz"] | refQz;
    refQw = doc["refQw"] | refQw;

    if (doc["reboot"] | false) {
      post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/config/reboot/clear", "{}");
      Serial.println("Remote reboot triggered");
      delay(500);
      ESP.restart();
    }
  }
  http.end();
}

// -------------------- NETWORK --------------------

void sendPostureChange() {
  get(String(SERVER_URL) + "/horses/" + HORSE_ID + "/readings/posture-change");
}

bool get(const String& url) {
  if (!ensureWifi()) return false;

  HTTPClient http;
  WiFiClientSecure secureClient;
  if (url.startsWith("https")) {
    secureClient.setInsecure();
    http.begin(secureClient, url);
  } else http.begin(url);
  http.setTimeout(HTTP_TIMEOUT_MS);

  int code = http.GET();
  if (code < 0) {
    Serial.println("  → failed: " + http.errorToString(code));
    http.end();
    return false;
  }

  if (code < 200 || code >= 300) {
    Serial.println("  → Body: " + http.getString());
    http.end();
    return false;
  }
  http.end();
  return true;
}

bool post(const String& url, const String& body) {
  if (!ensureWifi()) return false;

  HTTPClient http;
  WiFiClientSecure secureClient;
  if (url.startsWith("https")) {
    secureClient.setInsecure();
    http.begin(secureClient, url);
  } else http.begin(url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(body);
  Serial.println(code);
  Serial.println("→ Sending JSON:");
  Serial.println(body);
  if (code < 0) {
    Serial.println("  → failed: " + http.errorToString(code));
    http.end();
    return false;
  }
  if (code < 200 || code >= 300) {
    Serial.println("  → Body: " + http.getString());
    http.end();
    return false;
  }
  http.end();
  return true;
}

void sendHeartbeat() {
  get(String(SERVER_URL) + "/horses/" + HORSE_ID + "/heartbeat");
}

String isoTimestamp() {
  time_t now;
  time(&now);

  struct tm t;
  gmtime_r(&now, &t);

  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);

  return String(buf);
}

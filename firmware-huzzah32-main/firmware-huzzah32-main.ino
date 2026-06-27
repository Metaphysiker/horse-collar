// -------------------- INCLUDES --------------------

#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"


// -------------------- DEFINES --------------------

#define BNO_INT_PIN 14
#define BATTERY_PIN A13
#define WIFI_TIMEOUT_MS 12000         // Normal operation
#define WIFI_TIMEOUT_LOW_BAT_MS 6000  // Low-battery wakeups
#define HTTP_TIMEOUT_MS 5000
#define NTP_TIMEOUT_MS 10000
#define LOW_BATTERY_ENTER_V 3.55f
#define LOW_BATTERY_EXIT_V 3.75f
#define TIME_RESYNC_INTERVAL_US 3600000000ULL  // 1 hour


#define DEBUG_SERIAL 0  // Set to 1 for debug builds

#if DEBUG_SERIAL
#define LOG(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#define LOG_BEGIN() \
  do { \
    Serial.begin(115200); \
    delay(2000); \
  } while (0)
#define LOG_FLUSH() Serial.flush()
#else
#define LOG(fmt, ...) \
  do { \
  } while (0)
#define LOG_BEGIN() \
  do { \
  } while (0)
#define LOG_FLUSH() \
  do { \
  } while (0)
#endif


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

  float acceleration = 0.0f;
  float angularVelocity = 0.0f;

  String alertReason;
  String readingType;

  void toJson(JsonObject obj) const {
    obj["horseId"] = horseId;
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
  String posture;

  String toJson() const {
    JsonDocument doc;
    JsonObject raw = doc["rawReading"].to<JsonObject>();
    rawReading.toJson(raw);
    JsonObject normalized = doc["normalizedReading"].to<JsonObject>();
    normalizedReading.toJson(normalized);
    doc["posture"] = posture;
    String output;
    serializeJson(doc, output);
    return output;
  }
};


// -------------------- GLOBALS --------------------

Adafruit_BNO08x bno08x;
sh2_SensorValue_t event;
WiFiMulti wifiMulti;
WiFiClientSecure secureClient;

RTC_DATA_ATTR bool isCalibrated = false;

RTC_DATA_ATTR uint64_t lastHeartbeatUs = 0;

RTC_DATA_ATTR bool timeSynced = false;

SensorReadingV2Dto currentDto;

enum PowerMode {
  ACTIVE,
  MAINTENANCE,
  LOW_BATTERY
};

enum Posture { STANDING,
               LYING };
RTC_DATA_ATTR Posture posture = STANDING;
RTC_DATA_ATTR bool postureChanged = false;

// Calibration accumulators
RTC_DATA_ATTR static float calAccum[4] = { 0, 0, 0, 0 };
RTC_DATA_ATTR static int calCount = 0;
const int CAL_SAMPLES = 10;

RTC_DATA_ATTR int consecutiveMissedReadings = 0;
const int MISSED_READING_THRESHOLD = 3;

RTC_DATA_ATTR float weightGx = 1.0f;
RTC_DATA_ATTR float weightGy = 0.0f;
RTC_DATA_ATTR float weightGz = 0.0f;

RTC_DATA_ATTR uint64_t lastTimeSyncUs = 0;

// -------------------- CONFIG --------------------

RTC_DATA_ATTR float refQx = 0.0f, refQy = 0.0f, refQz = 0.0f, refQw = 1.0f;
RTC_DATA_ATTR bool useTiltForPosture = false;

RTC_DATA_ATTR uint32_t reportInterval = 1000000;
RTC_DATA_ATTR uint64_t sleepTimerUs = 1500000ULL;
RTC_DATA_ATTR uint64_t heartBeatInterval = 60000000ULL;

RTC_DATA_ATTR float rollEnterDeg = 75.0f;
RTC_DATA_ATTR float rollExitDeg = 60.0f;

RTC_DATA_ATTR PowerMode powerMode = ACTIVE;
RTC_DATA_ATTR uint64_t maintenanceWakeIntervalUs = 1800000000ULL;

RTC_DATA_ATTR bool bnoHealthy = false;


// -------------------- FORWARD DECLARATIONS --------------------

void firstBootSetup();
bool ensureWifi(uint32_t timeoutMs = WIFI_TIMEOUT_MS);
void connectWifi();
void disconnectWifi();
void syncTime();
void fetchConfig();
void calibrateAccumulate(float qx, float qy, float qz, float qw);
SensorReadingV2 normalize(const SensorReadingV2& raw);
Posture detectPostureV2(float nqw, float nqx, float nqy, float nqz);
Posture detectPostureV3(float nqw, float nqx, float nqy, float nqz);
Posture detectPostureV4(float nqw, float nqx, float nqy, float nqz);
bool sendReading(const SensorReadingV2Dto& dto);
bool sendPostureChange(const SensorReadingV2Dto& dto);
bool get(const String& url);
bool post(const String& url, const String& body);
Quat conjugate(const Quat& q);
Quat multiply(const Quat& a, const Quat& b);
String isoTimestamp();
float readBatteryVoltage();
int voltageToPercent(float voltage);
void sendDeviceStatus();
void runMaintenanceMode();
void checkBattery();


// -------------------- SETUP --------------------

void setup() {
  LOG_BEGIN();

  LOG("--- BNO085 Horse Collar Firmware ---\n");

  secureClient.setInsecure();

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  bool coldBoot = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);

  for (auto& n : WIFI_NETWORKS)
    wifiMulti.addAP(n.ssid, n.password);

  if (coldBoot) {
    firstBootSetup();
  }

  if (powerMode == MAINTENANCE || powerMode == LOW_BATTERY) {
    runMaintenanceMode();
  }

  Wire.begin();
  if (!bno08x.begin_I2C()) {
    LOG("BNO085 not found — deep sleeping 30s\n");
    LOG_FLUSH();
    esp_deep_sleep(30000000ULL);  // 30 seconds, almost zero current draw
  }
  LOG("BNO085 found.\n");

  if (!bno08x.enableReport(SH2_ROTATION_VECTOR, reportInterval)) {
    LOG("Failed to enable Rotation Vector!\n");
  } else {
    LOG("Rotation Vector enabled at 1 Hz.\n");
  }

  pinMode(BNO_INT_PIN, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BNO_INT_PIN, 0);
  esp_sleep_enable_timer_wakeup(sleepTimerUs);

  LOG("Setup complete. Entering sleep loop...\n");
  LOG_FLUSH();
}


// -------------------- LOOP --------------------

void loop() {
  checkBattery();
  if (WiFi.getMode() != WIFI_OFF) disconnectWifi();

  bool gotReading = false;
  int eventsProcessedThisLoop = 0;
  while (bno08x.getSensorEvent(&event)) {
    if (event.sensorId == SH2_ROTATION_VECTOR) {
      bnoHealthy = true;
      gotReading = true;
      consecutiveMissedReadings = 0;
      eventsProcessedThisLoop++;

      currentDto.rawReading.qx = event.un.rotationVector.i;
      currentDto.rawReading.qy = event.un.rotationVector.j;
      currentDto.rawReading.qz = event.un.rotationVector.k;
      currentDto.rawReading.qw = event.un.rotationVector.real;

      LOG("Quat: i=%+.3f j=%+.3f k=%+.3f real=%+.3f\n",
          currentDto.rawReading.qx,
          currentDto.rawReading.qy,
          currentDto.rawReading.qz,
          currentDto.rawReading.qw);

      if (!isCalibrated) {
        LOG("Cal sample %d / %d\n", calCount + 1, CAL_SAMPLES);
        calibrateAccumulate(currentDto.rawReading.qx,
                            currentDto.rawReading.qy,
                            currentDto.rawReading.qz,
                            currentDto.rawReading.qw);
        continue;
      }

      currentDto.rawReading.readingType = "Raw";
      currentDto.rawReading.horseId = HORSE_ID;

      currentDto.normalizedReading = normalize(currentDto.rawReading);
      currentDto.normalizedReading.readingType = "Normalized";
      currentDto.normalizedReading.horseId = HORSE_ID;

      Posture newPosture = detectPostureV4(
        currentDto.normalizedReading.qw,
        currentDto.normalizedReading.qx,
        currentDto.normalizedReading.qy,
        currentDto.normalizedReading.qz);

      if (newPosture != posture) {
        posture = newPosture;
        postureChanged = true;
      }
    }
  }

  if (!isCalibrated) {
    LOG_FLUSH();
    esp_light_sleep_start();
    return;
  }

  uint64_t now = esp_timer_get_time();
  bool needsHeartbeat = (now - lastHeartbeatUs >= heartBeatInterval);
  bool needsTimeResync = (now - lastTimeSyncUs >= TIME_RESYNC_INTERVAL_US);

  if (!gotReading) {
    consecutiveMissedReadings++;
    if (consecutiveMissedReadings >= MISSED_READING_THRESHOLD) {
      bnoHealthy = false;
    }
  }

  if (postureChanged || needsHeartbeat) {
    if (!timeSynced || needsTimeResync) syncTime();
    String ts = isoTimestamp();

    currentDto.rawReading.timestamp = ts;
    currentDto.normalizedReading.timestamp = ts;
    currentDto.posture = (posture == LYING ? "lying" : "standing");


    if (!ensureWifi()) {
      LOG("No WiFi — skipping transmission\n");
      // Don't clear postureChanged — retry next cycle
      // Still update lastHeartbeatUs to avoid hammering WiFi every 1.5s
      if (needsHeartbeat) lastHeartbeatUs = now;
      LOG_FLUSH();
      esp_light_sleep_start();
      return;
    }

    if (needsHeartbeat && gotReading) {
      if (!sendReading(currentDto)) {
        LOG("Send failed — will retry next heartbeat.\n");
      }
    }

    if (postureChanged) {
      if (sendPostureChange(currentDto)) {
        postureChanged = false;
      } else {
        LOG("Posture change send failed -- will retry next cycle\n");
      }
    }

    if (needsHeartbeat) {
      lastHeartbeatUs = now;
      sendDeviceStatus();
      fetchConfig();
      if (powerMode == MAINTENANCE) {
        ESP.restart();
      }
    }

    secureClient.stop();
    disconnectWifi();
  }

  LOG_FLUSH();
  esp_light_sleep_start();
}


// -------------------- BOOT --------------------

void firstBootSetup() {
  connectWifi();
  syncTime();
  fetchConfig();
  disconnectWifi();
}


// -------------------- WIFI --------------------

void connectWifi() {
  ensureWifi();
}

bool ensureWifi(uint32_t timeoutMs) {
  if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
  if (wifiMulti.run() == WL_CONNECTED) return true;

  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (wifiMulti.run() == WL_CONNECTED) return true;
    delay(500);
  }
  LOG("WiFi timeout\n");
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
      lastTimeSyncUs = esp_timer_get_time();
      return;
    }
    delay(500);
  }
  LOG("NTP sync failed\n");
}


// -------------------- CONFIG --------------------

void fetchConfig() {
  String url = String(SERVER_URL) + "/horses/" + HORSE_ID + "/config";

  HTTPClient http;
  secureClient.setInsecure();
  http.begin(secureClient, url);
  http.setTimeout(HTTP_TIMEOUT_MS);

  bool triggerRecalibrate = false;
  bool triggerReboot = false;
  bool bnoIntervalChanged = false;
  bool sleepTimerChanged = false;

  uint32_t targetReportInterval = reportInterval;
  uint64_t targetSleepTimerUs = sleepTimerUs;

  if (http.GET() == 200) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());

    targetReportInterval = doc["reportInterval"] | reportInterval;

    rollEnterDeg = doc["rollEnterDeg"] | rollEnterDeg;
    rollExitDeg = doc["rollExitDeg"] | rollExitDeg;

    targetSleepTimerUs = doc["sleepTimerUs"] | sleepTimerUs;
    heartBeatInterval = doc["heartBeatInterval"] | heartBeatInterval;
    useTiltForPosture = doc["useTiltForPosture"] | useTiltForPosture;

    weightGx = doc["weightGx"] | weightGx;
    weightGy = doc["weightGy"] | weightGy;
    weightGz = doc["weightGz"] | weightGz;

    String mode = doc["powerMode"] | "active";
    powerMode = (mode == "maintenance") ? MAINTENANCE : ACTIVE;

    maintenanceWakeIntervalUs = doc["maintenanceWakeIntervalUs"] | maintenanceWakeIntervalUs;

    if (doc["recalibrate"] | false) triggerRecalibrate = true;
    if (doc["reboot"] | false) triggerReboot = true;

    if (targetReportInterval != reportInterval) bnoIntervalChanged = true;
    if (targetSleepTimerUs != sleepTimerUs) sleepTimerChanged = true;
  }

  http.end();

  if (triggerRecalibrate) {
    isCalibrated = false;
    calCount = 0;
    memset(calAccum, 0, sizeof(calAccum));
    refQx = 0.0f;
    refQy = 0.0f;
    refQz = 0.0f;
    refQw = 1.0f;
    LOG("Remote recalibration triggered\n");
    post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/config/recalibrate/clear", "{}");
  }

  if (triggerReboot) {
    post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/config/reboot/clear", "{}");
    LOG("Remote reboot triggered\n");
    delay(500);
    ESP.restart();
  }

  if (bnoIntervalChanged) {
    reportInterval = targetReportInterval;
    LOG("Hardware Update: Changing BNO report interval to %u us\n", reportInterval);
    if (!bno08x.enableReport(SH2_ROTATION_VECTOR, reportInterval)) {
      LOG("Failed to update BNO report interval!\n");
    }
  }

  if (sleepTimerChanged) {
    sleepTimerUs = targetSleepTimerUs;
    LOG("Hardware Update: Changing ESP sleep timer to %llu us\n", sleepTimerUs);
    esp_sleep_enable_timer_wakeup(sleepTimerUs);
  }
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

  float len = sqrtf(refQx * refQx + refQy * refQy + refQz * refQz + refQw * refQw);
  refQx /= len;
  refQy /= len;
  refQz /= len;
  refQw /= len;

  isCalibrated = true;
  LOG("Calibrated. ref=(%.3f, %.3f, %.3f, %.3f)\n", refQx, refQy, refQz, refQw);
}


// -------------------- NORMALIZATION --------------------

SensorReadingV2 normalize(const SensorReadingV2& raw) {
  Quat current = { raw.qx, raw.qy, raw.qz, raw.qw };
  Quat reference = { refQx, refQy, refQz, refQw };
  Quat adj = multiply(conjugate(reference), current);

  SensorReadingV2 out = raw;
  out.qx = adj.x;
  out.qy = adj.y;
  out.qz = adj.z;
  out.qw = adj.w;
  return out;
}


// -------------------- POSTURE DETECTION --------------------

Posture detectPostureV2(float nqw, float nqx, float nqy, float nqz) {
  float gx = 2.0f * (nqx * nqz - nqy * nqw);
  float gy = 2.0f * (nqy * nqz + nqx * nqw);
  float gz = nqw * nqw - nqx * nqx - nqy * nqy + nqz * nqz;

  float rollDeg = asinf(constrain(fabsf(gx), 0.0f, 1.0f)) * 180.0f / PI;
  float pitchDeg = asinf(constrain(fabsf(gy), 0.0f, 1.0f)) * 180.0f / PI;

  LOG("ROLL=%+5.1f  PITCH=%+5.1f  gx=%+.3f gy=%+.3f gz=%+.3f  [%s]\n",
      gx >= 0 ? rollDeg : -rollDeg,
      gy >= 0 ? pitchDeg : -pitchDeg,
      gx, gy, gz,
      posture == LYING ? "LYING" : "STANDING");

  const float ENTER_SIN = sinf(rollEnterDeg * PI / 180.0f);
  const float EXIT_SIN = sinf(rollExitDeg * PI / 180.0f);

  switch (posture) {
    case STANDING:
      if (fabsf(gx) > ENTER_SIN) {
        LOG("-> LYING (roll %+.3f)\n", gx);
        return LYING;
      }
      break;
    case LYING:
      if (fabsf(gx) < EXIT_SIN) {
        LOG("-> STANDING (roll %+.3f)\n", gx);
        return STANDING;
      }
      break;
  }
  return posture;
}

Posture detectPostureV3(float nqw, float nqx, float nqy, float nqz) {
  float gx = 2.0f * (nqx * nqz - nqy * nqw);
  float gy = 2.0f * (nqy * nqz + nqx * nqw);
  float gz = nqw * nqw - nqx * nqx - nqy * nqy + nqz * nqz;

  float rollDeg = asinf(constrain(fabsf(gx), 0.0f, 1.0f)) * 180.0f / PI;
  float pitchDeg = asinf(constrain(fabsf(gy), 0.0f, 1.0f)) * 180.0f / PI;
  float tiltDeg = acosf(constrain(gz, -1.0f, 1.0f)) * 180.0f / PI;

  LOG("ROLL=%+5.1f  PITCH=%+5.1f  TILT=%+5.1f  gx=%+.3f gy=%+.3f gz=%+.3f  [%s]\n",
      gx >= 0 ? rollDeg : -rollDeg,
      gy >= 0 ? pitchDeg : -pitchDeg,
      tiltDeg, gx, gy, gz,
      posture == LYING ? "LYING" : "STANDING");

  float metric = useTiltForPosture ? tiltDeg : rollDeg;

  switch (posture) {
    case STANDING:
      if (metric > rollEnterDeg) {
        LOG("-> LYING (%s=%.1f)\n", useTiltForPosture ? "tilt" : "roll", metric);
        return LYING;
      }
      break;
    case LYING:
      if (metric < rollExitDeg) {
        LOG("-> STANDING (%s=%.1f)\n", useTiltForPosture ? "tilt" : "roll", metric);
        return STANDING;
      }
      break;
  }
  return posture;
}


// -------------------- NETWORK --------------------

bool get(const String& url) {
  if (!ensureWifi()) return false;

  HTTPClient http;
  if (url.startsWith("https")) {
    http.setReuse(true);
    http.begin(secureClient, url);
  } else {
    WiFiClient plainClient;
    http.begin(plainClient, url);
  }
  http.setTimeout(HTTP_TIMEOUT_MS);

  int code = http.GET();
  LOG("GET %s -> %d\n", url.c_str(), code);
  if (code < 0) {
    LOG("GET failed: %s\n", http.errorToString(code).c_str());
    http.end();
    return false;
  }
  if (code < 200 || code >= 300) {
    LOG("GET non-2xx: %s\n", http.getString().c_str());
    http.end();
    return false;
  }
  http.end();
  return true;
}

bool post(const String& url, const String& body) {
  LOG("POST %s\n", url.c_str());
  if (!ensureWifi()) return false;

  HTTPClient http;
  if (url.startsWith("https")) {
    http.setReuse(true);
    http.begin(secureClient, url);
  } else {
    WiFiClient plainClient;
    http.begin(plainClient, url);
  }
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(body);
  LOG("POST -> %d\n", code);
  if (code < 0) {
    LOG("POST failed: %s\n", http.errorToString(code).c_str());
    http.end();
    return false;
  }
  if (code < 200 || code >= 300) {
    LOG("POST non-2xx: %s\n", http.getString().c_str());
    http.end();
    return false;
  }
  http.end();
  return true;
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


// -------------------- TIMESTAMP --------------------

String isoTimestamp() {
  if (!timeSynced) {
    LOG("WARNING: timestamp not synced\n");
    return "1970-01-01T00:00:00Z";
  }
  time_t now;
  time(&now);
  struct tm t;
  gmtime_r(&now, &t);
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
  return String(buf);
}


// -------------------- BATTERY --------------------

float readBatteryVoltage() {
  return analogReadMilliVolts(BATTERY_PIN) * 2.0f / 1000.0f;
}

int voltageToPercent(float voltage) {
  if (voltage >= 4.25f) return 100;
  if (voltage <= 3.4f) return 0;
  return int((voltage - 3.4f) / (4.25f - 3.4f) * 100.0f);
}


// -------------------- SEND READINGS --------------------

bool sendReading(const SensorReadingV2Dto& dto) {
  String url = String(SERVER_URL) + "/v2/horses/" + HORSE_ID + "/readings/from-dto";
  return post(url, dto.toJson());
}


// -------------------- POSTURE CHANGE --------------------

bool sendPostureChange(const SensorReadingV2Dto& dto) {
  String url = String(SERVER_URL) + "/v2/horses/" + HORSE_ID + "/readings/posture-change";
  return post(url, dto.toJson());
}

void sendDeviceStatus() {
  float voltage = readBatteryVoltage();
  JsonDocument doc;
  doc["timestamp"] = isoTimestamp();
  doc["batteryVoltage"] = voltage;
  doc["batteryPercent"] = voltageToPercent(voltage);
  doc["bnoConnected"] = bnoHealthy;
  String body;
  serializeJson(doc, body);
  post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/status", body);
}

void runMaintenanceMode() {
  LOG("Maintenance mode wakeup\n");
  LOG("Wake cause: %d\n", esp_sleep_get_wakeup_cause());

  // ---- Low-battery path ----
  if (powerMode == LOW_BATTERY) {
    if (!ensureWifi(WIFI_TIMEOUT_LOW_BAT_MS)) {
      LOG("Low-bat: WiFi failed — sleeping immediately\n");
      esp_sleep_enable_timer_wakeup(maintenanceWakeIntervalUs);
      esp_deep_sleep_start();
    }

    if (!timeSynced) syncTime();

    float v = readBatteryVoltage();
    LOG("Low-battery wakeup: %.2fV\n", v);
    sendDeviceStatus();

    if (v >= LOW_BATTERY_EXIT_V) {
      LOG("Battery recovered — resuming ACTIVE mode\n");
      powerMode = ACTIVE;
      isCalibrated = false;
      calCount = 0;
      memset(calAccum, 0, sizeof(calAccum));
      disconnectWifi();
      delay(100);
      ESP.restart();
    }

    LOG("Still low (%.2fV) — sleeping %.1f min\n", v, maintenanceWakeIntervalUs / 60000000.0f);
    disconnectWifi();
    LOG_FLUSH();
    esp_sleep_enable_timer_wakeup(maintenanceWakeIntervalUs);
    esp_deep_sleep_start();
  }
  // ---- end low-battery block ----

  // ---- Normal maintenance path ----
  connectWifi();
  if (!timeSynced) syncTime();

  sendDeviceStatus();
  fetchConfig();
  disconnectWifi();

  if (powerMode == ACTIVE) {
    LOG("Leaving maintenance mode\n");
    isCalibrated = false;
    calCount = 0;
    memset(calAccum, 0, sizeof(calAccum));
    delay(100);
    ESP.restart();
  }

  LOG("Sleeping for %.1f minutes\n", maintenanceWakeIntervalUs / 60000000.0f);
  LOG_FLUSH();
  esp_sleep_enable_timer_wakeup(maintenanceWakeIntervalUs);
  esp_deep_sleep_start();
}


// -------------------- BATTERY GUARD --------------------

void checkBattery() {
  float v = readBatteryVoltage();
  if (powerMode != LOW_BATTERY && v < LOW_BATTERY_ENTER_V) {
    LOG("LOW BATTERY: %.2fV — entering low-battery sleep\n", v);
    powerMode = LOW_BATTERY;
    sendDeviceStatus();
    secureClient.stop();
    disconnectWifi();
    esp_sleep_enable_timer_wakeup(maintenanceWakeIntervalUs);
    esp_deep_sleep_start();
  }
}


// -------------------- POSTURE DETECTION V4 --------------------

Posture detectPostureV4(float nqw, float nqx, float nqy, float nqz) {
  float gx = 2.0f * (nqx * nqz - nqy * nqw);
  float gy = 2.0f * (nqy * nqz + nqx * nqw);
  float gz = nqw * nqw - nqx * nqx - nqy * nqy + nqz * nqz;

  float rollDeg = asinf(constrain(fabsf(gx), 0.0f, 1.0f)) * 180.0f / PI;
  float pitchDeg = asinf(constrain(fabsf(gy), 0.0f, 1.0f)) * 180.0f / PI;
  float tiltDeg = acosf(constrain(gz, -1.0f, 1.0f)) * 180.0f / PI;

  float customMetric = (fabsf(gx) * weightGx) + (fabsf(gy) * weightGy) + (fabsf(gz) * weightGz);
  float metricDeg = asinf(constrain(customMetric, 0.0f, 1.0f)) * 180.0f / PI;

  if (useTiltForPosture) {
    metricDeg = tiltDeg;
  }

  LOG("ROLL=%+5.1f PITCH=%+5.1f TILT=%+5.1f CUSTOM_METRIC_DEG=%5.1f [%s]\n",
      gx >= 0 ? rollDeg : -rollDeg,
      gy >= 0 ? pitchDeg : -pitchDeg,
      tiltDeg, metricDeg,
      posture == LYING ? "LYING" : "STANDING");

  switch (posture) {
    case STANDING:
      if (metricDeg > rollEnterDeg) {
        LOG("-> LYING (customMetric=%.1f)\n", metricDeg);
        return LYING;
      }
      break;
    case LYING:
      if (metricDeg < rollExitDeg) {
        LOG("-> STANDING (customMetric=%.1f)\n", metricDeg);
        return STANDING;
      }
      break;
  }
  return posture;
}
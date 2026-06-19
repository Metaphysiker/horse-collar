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

#define BNO_INT_PIN            14
#define BATTERY_PIN            A13
#define WIFI_TIMEOUT_MS        15000
#define HTTP_TIMEOUT_MS        5000
#define NTP_TIMEOUT_MS         10000
#define MAX_SEND_FAILURES      5


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
    JsonObject raw        = doc["rawReading"].to<JsonObject>();
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

Adafruit_BNO08x  bno08x;
sh2_SensorValue_t event;
WiFiMulti        wifiMulti;

RTC_DATA_ATTR bool    isCalibrated = false;

uint64_t lastHeartbeatUs      = 0;
int      readingsSinceLastSend = 0;
RTC_DATA_ATTR int      consecutiveSendFailures = 0;

RTC_DATA_ATTR bool   timeSynced = false;

RTC_DATA_ATTR bool firstBoot = true;

SensorReadingV2Dto currentDto;

enum Posture { STANDING, LYING };
Posture posture        = STANDING;
bool    postureChanged = false;

// Calibration accumulators
RTC_DATA_ATTR static float calAccum[4] = { 0, 0, 0, 0 };
RTC_DATA_ATTR static int   calCount    = 0;
const  int   CAL_SAMPLES = 10;

// -------------------- CONFIG --------------------

RTC_DATA_ATTR float   refQx = 0.0f, refQy = 0.0f, refQz = 0.0f, refQw = 1.0f;
RTC_DATA_ATTR bool useTiltForPosture = false;  // false = V2 roll-only, true = V3 tilt

uint32_t reportInterval = 1000000;  // 1 Hz
uint32_t sleepTimerUs = 1500000ULL; // One minute in microseconds
uint32_t heartBeatInterval = 60000000ULL; // One minute in microseconds
int      sendEveryN           = 60;

// Hysteresis thresholds (degrees of roll)
float rollEnterDeg = 75.0f;
float rollExitDeg  = 60.0f;

// -------------------- FORWARD DECLARATIONS --------------------

void    firstBootSetup();
bool    ensureWifi();
void    connectWifi();
void    disconnectWifi();
void    syncTime();
void    fetchConfig();
void    calibrateAccumulate(float qx, float qy, float qz, float qw);
SensorReadingV2 normalize(const SensorReadingV2& raw);
Posture detectPostureV2(float nqw, float nqx, float nqy, float nqz);
bool    sendReading(const SensorReadingV2Dto& dto);
bool    sendPostureChange(const SensorReadingV2Dto& dto);
void    sendHeartbeat();
bool    get(const String& url);
bool    post(const String& url, const String& body);
Quat    conjugate(const Quat& q);
Quat    multiply(const Quat& a, const Quat& b);
String  isoTimestamp();
float   readBatteryVoltage();
int     voltageToPercent(float voltage);
void    sendDeviceStatus(bool bnoConnected);


// -------------------- SETUP --------------------

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("--- BNO085 Horse Collar Firmware ---");

  Wire.begin();
  if (!bno08x.begin_I2C()) {
    Serial.println("BNO085 not found! Check wiring.");
    while (1) { delay(10); }
  }
  Serial.println("BNO085 found.");

  if (!bno08x.enableReport(SH2_ROTATION_VECTOR, reportInterval)) {
    Serial.println("Failed to enable Rotation Vector!");
  } else {
    Serial.println("Rotation Vector enabled at 1 Hz.");
  }

  // GPIO14 wakeup as fallback; primary wakeup is the 500 ms timer below
  pinMode(BNO_INT_PIN, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BNO_INT_PIN, 0);
  esp_sleep_enable_timer_wakeup(sleepTimerUs);

  for (auto& n : WIFI_NETWORKS)
    wifiMulti.addAP(n.ssid, n.password);

  if (firstBoot) {
    firstBootSetup();
  }

  Serial.println("Setup complete. Entering sleep loop...");
  Serial.flush();
}


// -------------------- LOOP --------------------

void loop() {
  // Re-init I2C only when waking from sleep (Wire state may be stale)
  Wire.begin();
  delay(10);

  bool gotReading = false;
  while (bno08x.getSensorEvent(&event)) {
    if (event.sensorId == SH2_ROTATION_VECTOR) {
      currentDto.rawReading.qx = event.un.rotationVector.i;
      currentDto.rawReading.qy = event.un.rotationVector.j;
      currentDto.rawReading.qz = event.un.rotationVector.k;
      currentDto.rawReading.qw = event.un.rotationVector.real;
      gotReading = true;

      Serial.printf("Quat: i=%+.3f j=%+.3f k=%+.3f real=%+.3f\n",
                    currentDto.rawReading.qx,
                    currentDto.rawReading.qy,
                    currentDto.rawReading.qz,
                    currentDto.rawReading.qw);

      if (!isCalibrated) {
        Serial.printf("Cal sample %d / %d\n", calCount + 1, CAL_SAMPLES);
        calibrateAccumulate(currentDto.rawReading.qx,
                            currentDto.rawReading.qy,
                            currentDto.rawReading.qz,
                            currentDto.rawReading.qw);
      }
    }
  }

  // During calibration, sleep and wait for more samples — don't burn CPU.
  if (!isCalibrated) {
    Serial.flush();
    esp_light_sleep_start();
    return;
  }

  if (!gotReading) {
    // No new sensor data this wake cycle — go back to sleep.
    Serial.flush();
    esp_light_sleep_start();
    return;
  }

  // ---- Build normalized reading ----
  currentDto.rawReading.readingType = "Raw";
  currentDto.rawReading.horseId     = HORSE_ID;

  currentDto.normalizedReading             = normalize(currentDto.rawReading);
  currentDto.normalizedReading.readingType = "Normalized";
  currentDto.normalizedReading.horseId     = HORSE_ID;

  // ---- Posture detection ----
  Posture newPosture = detectPostureV3(
    currentDto.normalizedReading.qw,
    currentDto.normalizedReading.qx,
    currentDto.normalizedReading.qy,
    currentDto.normalizedReading.qz);

  if (newPosture != posture) {
    posture        = newPosture;
    postureChanged = true;
  }

  // ---- Decide whether to transmit ----
  uint64_t now           = esp_timer_get_time();
  bool     needsHeartbeat = (now - lastHeartbeatUs >= heartBeatInterval);

  readingsSinceLastSend++;
  if (readingsSinceLastSend >= sendEveryN || postureChanged || needsHeartbeat) {
    readingsSinceLastSend = 0;

    if (!timeSynced) syncTime();
    String ts = isoTimestamp();

    currentDto.rawReading.timestamp        = ts;
    currentDto.normalizedReading.timestamp = ts;
    currentDto.posture = (posture == LYING ? "lying" : "standing");

    connectWifi();

    bool ok = sendReading(currentDto);
    if (ok) {
      consecutiveSendFailures = 0;
    } else {
      consecutiveSendFailures++;
      Serial.printf("Send failed -- consecutive failures: %d\n", consecutiveSendFailures);
      if (consecutiveSendFailures >= MAX_SEND_FAILURES) {
        Serial.printf("Too many failures (%d) -- rebooting in 60s\n", consecutiveSendFailures);
        Serial.flush();
        delay(60000);
        ESP.restart();
      }
    }

    if (postureChanged) {
      if (sendPostureChange(currentDto)) {
        postureChanged = false;
      } else {
        Serial.println("Posture change send failed -- will retry next cycle");
      }
    }

    if (needsHeartbeat) {
      lastHeartbeatUs = now;
      sendHeartbeat();
    }

    disconnectWifi();
  }

  Serial.flush();
  esp_light_sleep_start();
}


// -------------------- BOOT --------------------

void firstBootSetup() {
  firstBoot = false;
  connectWifi();
  syncTime();
  fetchConfig();
  disconnectWifi();
}


// -------------------- WIFI --------------------

void connectWifi() {
  ensureWifi();
}

bool ensureWifi() {
  if (WiFi.getMode() != WIFI_STA) {
    WiFi.mode(WIFI_STA);
  }
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  if (wifiMulti.run() == WL_CONNECTED) return true;

  unsigned long start = millis();
  while (millis() - start < WIFI_TIMEOUT_MS) {
    esp_task_wdt_reset();
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
    esp_task_wdt_reset();
    time(&now);
    if (now > 100000) {
      timeSynced = true;
      return;
    }
    delay(500);
  }
  Serial.println("NTP sync failed");
}


// -------------------- CONFIG --------------------

void fetchConfig() {
  String url = String(SERVER_URL) + "/horses/" + HORSE_ID + "/config";

  HTTPClient      http;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  http.begin(secureClient, url);
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (http.GET() == 200) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());

    reportInterval = doc["reportInterval"] | reportInterval;
    sendEveryN = doc["sendEveryN"] | sendEveryN;
    //refQx      = doc["refQx"]      | refQx;
    //refQy      = doc["refQy"]      | refQy;
    //refQz      = doc["refQz"]      | refQz;
    //refQw      = doc["refQw"]      | refQw;

    rollEnterDeg = doc["rollEnterDeg"] | rollEnterDeg;
    rollExitDeg = doc["rollExitDeg"] | rollExitDeg;

    sleepTimerUs = doc["sleepTimerUs"] | sleepTimerUs;
    heartBeatInterval = doc["heartBeatInterval"] | heartBeatInterval;

    useTiltForPosture = doc["useTiltForPosture"] | useTiltForPosture;

    if (doc["recalibrate"] | false) {
      isCalibrated = false;
      calCount     = 0;
      memset(calAccum, 0, sizeof(calAccum));
      refQx = 0.0f; refQy = 0.0f; refQz = 0.0f; refQw = 1.0f; // ADD THIS
      Serial.println("Remote recalibration triggered");
    }

    if (doc["reboot"] | false) {
      post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/config/reboot/clear", "{}");
      Serial.println("Remote reboot triggered");
      delay(500);
      ESP.restart();
    }
  }
  http.end();
}


// -------------------- CALIBRATION --------------------

// NOTE: Simple component averaging is valid here because samples are taken
// while the horse is stationary, so all quaternions are very close together.
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

  float len = sqrtf(refQx*refQx + refQy*refQy + refQz*refQz + refQw*refQw);
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
  Quat reference = { refQx, refQy, refQz, refQw };
  Quat adj       = multiply(conjugate(reference), current);

  SensorReadingV2 out = raw;
  out.qx = adj.x;
  out.qy = adj.y;
  out.qz = adj.z;
  out.qw = adj.w;
  return out;
}


// -------------------- POSTURE DETECTION --------------------

Posture detectPostureV2(float nqw, float nqx, float nqy, float nqz) {
  // Gravity vector in device frame (3rd column of rotation matrix):
  //   gx = roll  (side-to-side, +right / -left)
  //   gy = pitch (fore-aft, ignored for posture)
  //   gz = vertical uprightness
  float gx = 2.0f * (nqx * nqz - nqy * nqw);
  float gy = 2.0f * (nqy * nqz + nqx * nqw);
  float gz = nqw*nqw - nqx*nqx - nqy*nqy + nqz*nqz;

  float rollDeg  = asinf(constrain(fabsf(gx), 0.0f, 1.0f)) * 180.0f / PI;
  float pitchDeg = asinf(constrain(fabsf(gy), 0.0f, 1.0f)) * 180.0f / PI;

  Serial.printf("ROLL=%+5.1f  PITCH=%+5.1f  gx=%+.3f gy=%+.3f gz=%+.3f  [%s]\n",
                gx >= 0 ? rollDeg : -rollDeg,
                gy >= 0 ? pitchDeg : -pitchDeg,
                gx, gy, gz,
                posture == LYING ? "LYING" : "STANDING");

  const float ENTER_SIN = sinf(rollEnterDeg * PI / 180.0f);  // ~0.966
  const float EXIT_SIN  = sinf(rollExitDeg  * PI / 180.0f);  // ~0.866

  switch (posture) {
    case STANDING:
      if (fabsf(gx) > ENTER_SIN) {
        Serial.printf("-> LYING (roll %+.3f)\n", gx);
        return LYING;
      }
      break;

    case LYING:
      if (fabsf(gx) < EXIT_SIN) {
        Serial.printf("-> STANDING (roll %+.3f)\n", gx);
        return STANDING;
      }
      break;
  }
  return posture;
}

Posture detectPostureV3(float nqw, float nqx, float nqy, float nqz) {
  float gx = 2.0f * (nqx * nqz - nqy * nqw);
  float gy = 2.0f * (nqy * nqz + nqx * nqw);
  float gz = nqw*nqw - nqx*nqx - nqy*nqy + nqz*nqz;

  float rollDeg  = asinf(constrain(fabsf(gx), 0.0f, 1.0f)) * 180.0f / PI;
  float pitchDeg = asinf(constrain(fabsf(gy), 0.0f, 1.0f)) * 180.0f / PI;
  float tiltDeg  = acosf(constrain(gz, -1.0f, 1.0f)) * 180.0f / PI;

  Serial.printf("ROLL=%+5.1f  PITCH=%+5.1f  TILT=%+5.1f  gx=%+.3f gy=%+.3f gz=%+.3f  [%s]\n",
                gx >= 0 ? rollDeg : -rollDeg,
                gy >= 0 ? pitchDeg : -pitchDeg,
                tiltDeg, gx, gy, gz,
                posture == LYING ? "LYING" : "STANDING");

  float metric = useTiltForPosture ? tiltDeg : rollDeg;

  switch (posture) {
    case STANDING:
      if (metric > rollEnterDeg) {
        Serial.printf("-> LYING (%s=%.1f)\n", useTiltForPosture ? "tilt" : "roll", metric);
        return LYING;
      }
      break;

    case LYING:
      if (metric < rollExitDeg) {
        Serial.printf("-> STANDING (%s=%.1f)\n", useTiltForPosture ? "tilt" : "roll", metric);
        return STANDING;
      }
      break;
  }
  return posture;
}


// -------------------- NETWORK --------------------

bool get(const String& url) {
  if (!ensureWifi()) return false;

  HTTPClient       http;
  WiFiClientSecure secureClient;
  if (url.startsWith("https")) {
    secureClient.setInsecure();
    http.begin(secureClient, url);
  } else {
    http.begin(url);
  }
  http.setTimeout(HTTP_TIMEOUT_MS);

  int code = http.GET();
  Serial.printf("GET %s -> %d\n", url.c_str(), code);
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

  HTTPClient       http;
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
  Serial.printf("POST -> %d\n", code);
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


// -------------------- QUATERNION --------------------

Quat conjugate(const Quat& q) {
  return { -q.x, -q.y, -q.z, q.w };
}

Quat multiply(const Quat& a, const Quat& b) {
  return {
    a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
    a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
    a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
    a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
  };
}


// -------------------- TIMESTAMP --------------------

String isoTimestamp() {
  if (!timeSynced) {
    Serial.println("WARNING: timestamp not synced");
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
  if (voltage <= 3.4f)  return 0;
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


// -------------------- HEARTBEAT --------------------

void sendHeartbeat() {
  sendDeviceStatus(true);
  fetchConfig();
}

void sendDeviceStatus(bool bnoConnected) {
  float voltage = readBatteryVoltage();
  JsonDocument doc;
  doc["timestamp"]      = isoTimestamp();
  doc["batteryVoltage"] = voltage;
  doc["batteryPercent"] = voltageToPercent(voltage);
  doc["bnoConnected"]   = bnoConnected;
  String body; serializeJson(doc, body);
  post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/status", body);
}

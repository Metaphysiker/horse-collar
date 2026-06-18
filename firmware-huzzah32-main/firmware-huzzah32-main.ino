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
#include <esp_system.h>


// -------------------- DEFINES --------------------

#define BNO_INT_PIN 14
#define BATTERY_PIN A13
#define NTP_TIMEOUT_MS 10000
#define WIFI_TIMEOUT_MS 15000
#define HTTP_TIMEOUT_MS 5000
#define NTP_TIMEOUT_MS 10000
#define HEARTBEAT_INTERVAL_US  (60ULL * 1000000ULL)   // 60 s in microseconds
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

Adafruit_BNO08x bno08x;
uint32_t reportInterval = 1000000;
sh2_SensorValue_t event;
WiFiMulti wifiMulti;
bool isCalibrated = false;
float refQx = 0.0f;
float refQy = 0.0f;
float refQz = 0.0f;
float refQw = 1.0f;
uint64_t lastHeartbeatUs = 0;
uint64_t lastProcessedUs = 0;
int sendEveryN = 60;
int readingsSinceLastSend = 0;
RTC_DATA_ATTR bool firstBoot = true;
bool timeSynced = false;
SensorReadingV2Dto currentDto;
enum Posture { STANDING,
               LYING };
Posture posture = STANDING;
bool postureChanged = false;

static float calAccum[4] = { 0, 0, 0, 0 };
static int calCount = 0;
const int CAL_SAMPLES = 10;

const float ROLL_ENTER_DEG = 75.0f;
const float ROLL_EXIT_DEG = 60.0f;

static int consecutiveSendFailures = 0;


// -------------------- FORWARD DECLARATIONS --------------------


// -------------------- FUNCTIONS --------------------

float readBatteryVoltage() {
  return analogReadMilliVolts(BATTERY_PIN) * 2.0f / 1000.0f;
}

int voltageToPercent(float voltage) {
  if (voltage >= 4.25f) return 100;
  if (voltage <= 3.4f) return 0;
  return int((voltage - 3.4f) / (4.25f - 3.4f) * 100);
}


void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for serial monitor to open
  Serial.println("--- Minimal 1Hz BNO085 Test ---");

  Wire.begin();
  if (!bno08x.begin_I2C()) {
    Serial.println("BNO085 not found! Check wiring.");
    while (1) { delay(10); }  // Halt if not found
  }
  Serial.println("BNO085 found.");

  // Enable Rotation Vector at 1 second (1,000,000 microseconds)
  if (bno08x.enableReport(SH2_ROTATION_VECTOR, reportInterval)) {
    Serial.println("Rotation Vector enabled at 1Hz (1,000,000 us).");
  } else {
    Serial.println("Failed to enable Rotation Vector!");
  }

  // Setup INT pin to wake from light sleep
  pinMode(BNO_INT_PIN, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BNO_INT_PIN, 0);  // 0 = LOW level

  for (auto& n : WIFI_NETWORKS)
    wifiMulti.addAP(n.ssid, n.password);

  if (firstBoot) {
    firstBootSetup();
  }

  Serial.println("Setup complete. Entering sleep loop...");
  Serial.flush();
}

int getBatteryPercent() {
  float batteryVoltage = readBatteryVoltage();            // Get the current reading
  int batteryPercent = voltageToPercent(batteryVoltage);  // Convert to 0-100%

  // Print both values to Serial for debugging or monitoring
  Serial.printf("Battery: %.2fV (%d%%)\n", batteryVoltage, batteryPercent);
  return batteryPercent;
}

void loop() {

  Wire.begin();
  delay(10);  // Small delay to let I2C and sensor stabilize


  while (bno08x.getSensorEvent(&event)) {
    if (event.sensorId == SH2_ROTATION_VECTOR) {
      Serial.printf("Quat: i=%+.3f, j=%+.3f, k=%+.3f, real=%+.3f\n",
                    event.un.rotationVector.i,
                    event.un.rotationVector.j,
                    event.un.rotationVector.k,
                    event.un.rotationVector.real);

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
    }
  }

  if (!isCalibrated) return;

  uint64_t now = esp_timer_get_time();


  currentDto.rawReading.readingType = "Raw";
  currentDto.rawReading.horseId = HORSE_ID;

  currentDto.normalizedReading = normalize(currentDto.rawReading);
  currentDto.normalizedReading.readingType = "Normalized";
  currentDto.normalizedReading.horseId = HORSE_ID;

  float nqw = currentDto.normalizedReading.qw;
  float nqx = currentDto.normalizedReading.qx;
  float nqy = currentDto.normalizedReading.qy;
  float nqz = currentDto.normalizedReading.qz;
  float gz = nqw * nqw - nqx * nqx - nqy * nqy + nqz * nqz;

  Serial.printf("gz=%.3f  posture=%s\n", gz,
                posture == LYING ? "LYING" : "STANDING");

  posture = detectPostureV2(nqw, nqx, nqy, nqz);


  bool needsHeartbeat = (now - lastHeartbeatUs >= HEARTBEAT_INTERVAL_US);

  readingsSinceLastSend++;
  if (readingsSinceLastSend >= sendEveryN || postureChanged || needsHeartbeat) {
    readingsSinceLastSend = 0;
    String ts = isoTimestamp();
    currentDto.rawReading.timestamp = ts;
    currentDto.normalizedReading.timestamp = ts;

    connectWifi();

    bool ok = sendReading(currentDto);
    if (ok) {
      consecutiveSendFailures = 0;
    } else {
      consecutiveSendFailures++;
      Serial.printf("Send failed -- consecutive failures: %d\n",
                    consecutiveSendFailures);
      if (consecutiveSendFailures >= MAX_SEND_FAILURES) {
        Serial.println("Too many failures -- rebooting");
        delay(200);
        ESP.restart();
      }
    }

    if (postureChanged) {
      sendPostureChange();
      postureChanged = false;
    }
    if (needsHeartbeat) {
      lastHeartbeatUs = now;
      sendHeartbeat();
    }

    disconnectWifi();
  }

  Serial.flush();  // CRITICAL: Ensure serial prints finish before sleeping
  esp_light_sleep_start();
}

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
  if (WiFi.getMode() == WIFI_OFF) {
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
  }
  if (wifiMulti.run() == WL_CONNECTED) return true;

  unsigned long start = millis();
  while (millis() - start < WIFI_TIMEOUT_MS) {
    esp_task_wdt_reset();  // keep WDT happy during connect wait
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

    if (doc["recalibrate"] | false) {
      isCalibrated = false;
      calCount = 0;
      memset(calAccum, 0, sizeof(calAccum));
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

  float len = sqrt(refQx * refQx + refQy * refQy + refQz * refQz + refQw * refQw);
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

Posture detectPostureV2(float nqw, float nqx, float nqy, float nqz) {
  // Gravity vector components in device frame (3rd column of rotation matrix):
  //   gx = roll  component: side-to-side tilt  (+right, -left)
  //   gy = pitch component: fore-aft tilt       (ignored for posture)
  //   gz = vertical component: upright-ness
  float gx = 2.0f * (nqx * nqz - nqy * nqw);  // signed roll
  float gy = 2.0f * (nqy * nqz + nqx * nqw);  // signed pitch
  float gz = nqw * nqw - nqx * nqx - nqy * nqy + nqz * nqz;

  // Keep gx signed so log shows direction of tilt (+right / -left).
  float rollTilt = gx;

  float rollDeg = asinf(constrain(fabsf(rollTilt), 0.0f, 1.0f)) * 180.0f / PI;
  float pitchDeg = asinf(constrain(fabsf(gy), 0.0f, 1.0f)) * 180.0f / PI;

  Serial.printf(
    "ROLL=%+5.1f  PITCH=%+5.1f  gx=%+.3f gy=%+.3f gz=%+.3f  [%s]\n",
    rollTilt >= 0 ? rollDeg : -rollDeg,
    gy >= 0 ? pitchDeg : -pitchDeg,
    gx, gy, gz,
    posture == LYING ? "LYING" : "STANDING");

  const float ENTER_SIN = sinf(ROLL_ENTER_DEG * PI / 180.0f);  // ~= 0.966
  const float EXIT_SIN = sinf(ROLL_EXIT_DEG * PI / 180.0f);    // ~= 0.866

  switch (posture) {
    case STANDING:
      if (fabsf(rollTilt) > ENTER_SIN) {
        Serial.printf("-> LYING (roll %+.3f)\n", rollTilt);
        postureChanged = true;
        return LYING;
      }
      break;

    case LYING:
      if (fabsf(rollTilt) < EXIT_SIN) {
        Serial.printf("-> STANDING (roll %+.3f)\n", rollTilt);
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
    a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
    a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
  };
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


// -------------------- HEARTBEAT --------------------

void sendHeartbeat() {
  get(String(SERVER_URL) + "/horses/" + HORSE_ID + "/heartbeat");
  fetchConfig();
}

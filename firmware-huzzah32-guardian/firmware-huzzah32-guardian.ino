#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_BNO08x.h>
#include <time.h>
#include "config.infomaniak.h"  // swap to config.development.h for local development

#define BATTERY_PIN     A13
#define WIFI_TIMEOUT_MS 15000
#define NTP_TIMEOUT_MS  10000
#define HTTP_TIMEOUT_MS 5000
#define MAX_BUFFER      200   // max buffered readings in RTC (~6400 bytes)

enum HorseState { Standing, LyingDown, Moving, Rolling, Alert, Emergency };

// ── Buffered reading stored in RTC ────────────────────────────────────────────

struct BufferedReading {
  int32_t  unixTime;
  float    pitch;
  float    roll;
  float    acceleration;
  float    angularVelocity;
  int16_t  stillCycles;
  uint8_t  state;
  uint8_t  alertReasonIdx;   // 0=none, 1=BaselineShift, 2=SuddenFall, 3=ActivityCollapse, 4=ColicRolling
};

// ── RTC memory — survives deep sleep ──────────────────────────────────────────

RTC_DATA_ATTR bool        firstBoot           = true;
RTC_DATA_ATTR HorseState  currentState        = Standing;
RTC_DATA_ATTR int         stillCycles         = 0;   // consecutive cycles with very low accel
RTC_DATA_ATTR int         cycleCount          = 0;
RTC_DATA_ATTR bool        timeSynced          = false;

// Config — stored in RTC so firmware doesn't need to fetch every wake
RTC_DATA_ATTR int   sleepNormalS         = 1;
RTC_DATA_ATTR int   sendEveryN           = 60;
RTC_DATA_ATTR float changeAccel          = 1.5f;
RTC_DATA_ATTR float changePitch          = 25.0f;
RTC_DATA_ATTR float changeRoll           = 25.0f;

// Strategy counters
RTC_DATA_ATTR int   rollingCount         = 0;
RTC_DATA_ATTR int   rollingWindowCycles  = 0;
RTC_DATA_ATTR int   activeCycles         = 0;
RTC_DATA_ATTR int   postActiveCycles     = 0;
RTC_DATA_ATTR float emaAccel             = 0;
RTC_DATA_ATTR float emaPitch             = 0;
RTC_DATA_ATTR float emaRoll              = 0;
RTC_DATA_ATTR int   settledCycles        = 0;

// Buffer
RTC_DATA_ATTR int             bufferCount  = 0;
RTC_DATA_ATTR BufferedReading buffer[MAX_BUFFER];

// Detection thresholds — purely dynamics-based, no calibration needed
#define ACCEL_ROLLING      1.5f   // vigorous movement
#define ACCEL_MOVING       0.3f   // any movement
#define ACCEL_STILL        0.2f   // stationary
#define GYRO_ROLLING       0.3f   // rotating
#define ROLLING_ALERT_N    8
#define ROLLING_WINDOW_N   40
#define ACTIVE_MIN_CYCLES  6
#define SUDDEN_STOP_N      6
#define EMA_ALPHA          0.2f
#define EMA_SETTLE_N       5
// CHANGE_ACCEL / CHANGE_PITCH / CHANGE_ROLL are in RTC (fetched from server config)

// ── Runtime (reset each wake) ──────────────────────────────────────────────────

WiFiMulti wifiMulti;
Adafruit_BNO08x bno;

float curQw = 1, curQx = 0, curQy = 0, curQz = 0;
float pitch = 0, roll = 0, acceleration = 0, angularVelocity = 0, temperature = 0;
float batteryVoltage = 0;
const char* alertReason = nullptr;

// ── Setup — runs once per wake cycle ──────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  batteryVoltage = readBatteryVoltage();
  initBno();

  if (firstBoot) {
    connectWifi();
    syncTime();
    fetchConfig();
    readSensor();
    currentState = detectState();
    sendReading(currentState);
    sendDeviceStatus();
    disconnectWifi();
    firstBoot = false;
    Serial.printf("Sleeping %ds (first boot done)\n", sleepNormalS);
    esp_sleep_enable_timer_wakeup((uint64_t)sleepNormalS * 1000000ULL);
    esp_deep_sleep_start();
  }

  readSensor();

  HorseState newState = detectState();
  bool stateChanged   = newState != currentState;
  cycleCount++;

  // Track consecutive still cycles (useful for spotting prolonged immobility)
  if (acceleration < ACCEL_STILL)
    stillCycles++;
  else
    stillCycles = 0;

  Serial.printf("pitch:%6.1f  roll:%6.1f  accel:%.2f  gyro:%.2f  still:%d  state:%s%s\n",
    pitch, roll, acceleration, angularVelocity, stillCycles, stateToString(newState),
    alertReason ? (String(" [") + alertReason + "]").c_str() : "");

  bool isUrgent = (newState == Alert || newState == Emergency);

  if (isUrgent) {
    // Send alert solo so ntfy fires on the backend, flush buffered history first
    currentState = newState;
    connectWifi();
    if (!timeSynced) syncTime();
    flushBuffer();
    sendReading(newState);
    sendDeviceStatus();
    fetchConfig();
    disconnectWifi();
  } else {
    pushToBuffer(newState);
    bool sendDue = (sendEveryN > 0 && cycleCount % sendEveryN == 0) || (bufferCount >= MAX_BUFFER);
    if (sendDue || stateChanged) {
      currentState = newState;
      connectWifi();
      if (!timeSynced) syncTime();
      flushBuffer();
      sendDeviceStatus();
      fetchConfig();
      disconnectWifi();
    } else {
      currentState = newState;
    }
  }

  int sleepSeconds = sleepNormalS;
  if (newState == Alert)     sleepSeconds = max(5, sleepNormalS / 2);
  if (newState == Emergency) sleepSeconds = max(5, sleepNormalS / 3);

  Serial.printf("Sleeping %ds  buffer: %d/%d  cycle: %d (send every %d)\n",
    sleepSeconds, bufferCount, MAX_BUFFER, cycleCount % max(1, sendEveryN), sendEveryN);
  esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {}  // never reached — deep sleep restarts setup()

// ── WiFi ──────────────────────────────────────────────────────────────────────

bool ensureWifi() {
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  if (wifiMulti.run() == WL_CONNECTED) return true;

  Serial.print("Connecting WiFi");
  unsigned long start = millis();
  while (millis() - start < WIFI_TIMEOUT_MS) {
    if (wifiMulti.run() == WL_CONNECTED) {
      Serial.println(" connected to " + WiFi.SSID());
      return true;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println(" failed");
  return false;
}

void disconnectWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void connectWifi() {
  struct { const char* ssid; const char* password; } networks[] = WIFI_NETWORKS;
  for (auto& n : networks) wifiMulti.addAP(n.ssid, n.password);
  ensureWifi();
}

// ── Time ──────────────────────────────────────────────────────────────────────

void syncTime() {
  if (!ensureWifi()) return;
  configTime(TZ_OFFSET, 0, NTP_SERVER);
  Serial.print("Syncing time");
  unsigned long start = millis();
  time_t now = 0;
  while (millis() - start < NTP_TIMEOUT_MS) {
    time(&now);
    if (now > 100000) { timeSynced = true; Serial.println(" done"); return; }
    delay(500); Serial.print(".");
  }
  Serial.println(" failed");
}

// ── BNO085 ────────────────────────────────────────────────────────────────────

void initBno() {
  if (!bno.begin_I2C()) {
    Serial.println("BNO085 not found — check wiring");
    while (1) delay(100);
  }
  bno.enableReport(SH2_ROTATION_VECTOR);
  bno.enableReport(SH2_LINEAR_ACCELERATION);
  bno.enableReport(SH2_GYROSCOPE_CALIBRATED);
  delay(100);
}

void readSensor() {
  sh2_SensorValue_t event;
  bool gotRotation = false, gotAccel = false;
  unsigned long start = millis();
  while (millis() - start < 3000 && (!gotRotation || !gotAccel)) {
    if (bno.getSensorEvent(&event)) {
      if (event.sensorId == SH2_ROTATION_VECTOR) {
        curQw = event.un.rotationVector.real;
        curQx = event.un.rotationVector.i;
        curQy = event.un.rotationVector.j;
        curQz = event.un.rotationVector.k;
        pitch = atan2(2*(curQw*curQx + curQy*curQz), 1 - 2*(curQx*curQx + curQy*curQy)) * 180.0f / PI;
        roll  = asin(constrain(2*(curQw*curQy - curQz*curQx), -1.0f, 1.0f)) * 180.0f / PI;
        gotRotation = true;
      }
      if (event.sensorId == SH2_LINEAR_ACCELERATION) {
        acceleration = sqrt(
          pow(event.un.linearAcceleration.x, 2) +
          pow(event.un.linearAcceleration.y, 2) +
          pow(event.un.linearAcceleration.z, 2)
        );
        gotAccel = true;
      }
      if (event.sensorId == SH2_GYROSCOPE_CALIBRATED) {
        angularVelocity = sqrt(
          pow(event.un.gyroscope.x, 2) +
          pow(event.un.gyroscope.y, 2) +
          pow(event.un.gyroscope.z, 2)
        );
      }
    } else {
      delay(5);
    }
  }
  if (!gotRotation) {
    Serial.println("BNO085 timed out — restarting");
    ESP.restart();
  }
}

// ── State detection ───────────────────────────────────────────────────────────

HorseState detectState() {
  bool rolling    = acceleration > ACCEL_ROLLING && angularVelocity > GYRO_ROLLING;
  bool moving     = acceleration > ACCEL_MOVING;
  bool stationary = acceleration < ACCEL_STILL;
  alertReason = nullptr;

  // [5 — least aggressive] ColicRolling: repeated rolling in short window
  rollingWindowCycles++;
  if (rollingWindowCycles > ROLLING_WINDOW_N) {
    rollingWindowCycles = 1;
    rollingCount = 0;
  }

  if (rolling) {
    postActiveCycles = 0;
    rollingCount++;
    activeCycles++;
    if (rollingCount >= ROLLING_ALERT_N) {
      alertReason = "ColicRolling";
      rollingCount = 0;
      return Alert;
    }
    return Rolling;
  }

  // [3] ActivityCollapse: was energetically moving, then abruptly stopped
  if (acceleration > ACCEL_ROLLING) {
    activeCycles++;
    postActiveCycles = 0;
  } else if (activeCycles >= ACTIVE_MIN_CYCLES) {
    postActiveCycles++;
    if (postActiveCycles >= SUDDEN_STOP_N) {
      activeCycles     = 0;
      postActiveCycles = 0;
      alertReason = "ActivityCollapse";
      return Alert;
    }
  } else {
    activeCycles = max(0, activeCycles - 1);
  }

  // [2] SuddenFall: was rolling, now stationary — instant
  if (stationary && currentState == Rolling) {
    alertReason = "SuddenFall";
    return Alert;
  }

  // [1 — most aggressive] BaselineShift: deviation from EMA of accel + pitch + roll
  if (settledCycles == 0) {
    // Seed EMA with actual values so warmup starts from reality, not 0
    emaAccel = acceleration;
    emaPitch = pitch;
    emaRoll  = roll;
    settledCycles++;
  } else if (settledCycles < EMA_SETTLE_N) {
    emaAccel = EMA_ALPHA * acceleration + (1 - EMA_ALPHA) * emaAccel;
    emaPitch = EMA_ALPHA * pitch        + (1 - EMA_ALPHA) * emaPitch;
    emaRoll  = EMA_ALPHA * roll         + (1 - EMA_ALPHA) * emaRoll;
    settledCycles++;
  } else {
    float da = abs(acceleration - emaAccel);
    float dp = abs(pitch        - emaPitch);
    float dr = abs(roll         - emaRoll);
    emaAccel = EMA_ALPHA * acceleration + (1 - EMA_ALPHA) * emaAccel;
    emaPitch = EMA_ALPHA * pitch        + (1 - EMA_ALPHA) * emaPitch;
    emaRoll  = EMA_ALPHA * roll         + (1 - EMA_ALPHA) * emaRoll;
    if (da > changeAccel || dp > changePitch || dr > changeRoll) {
      alertReason = "BaselineShift";
      return Alert;
    }
  }

  return moving ? Moving : Standing;
}

// ── Battery ───────────────────────────────────────────────────────────────────

float readBatteryVoltage() {
  return analogReadMilliVolts(BATTERY_PIN) * 2.0f / 1000.0f;
}

int voltageToPercent(float v) {
  if (v >= 4.2f) return 100;
  if (v >= 3.9f) return (int)((v - 3.9f) / 0.3f * 40) + 60;
  if (v >= 3.7f) return (int)((v - 3.7f) / 0.2f * 30) + 30;
  if (v >= 3.4f) return (int)((v - 3.4f) / 0.3f * 25) + 5;
  return 0;
}

// ── HTTP ──────────────────────────────────────────────────────────────────────

String isoTimestamp() {
  time_t now; time(&now);
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", gmtime(&now));
  return String(buf);
}

String isoFromUnix(int32_t t) {
  time_t ts = (time_t)t;
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", gmtime(&ts));
  return String(buf);
}

const char* stateToString(HorseState s) {
  switch (s) {
    case Standing:  return "Standing";
    case LyingDown: return "LyingDown";
    case Moving:    return "Moving";
    case Rolling:   return "Rolling";
    case Alert:     return "Alert";
    case Emergency: return "Emergency";
    default:        return "Standing";
  }
}

uint8_t alertReasonToIdx(const char* reason) {
  if (!reason)                                return 0;
  if (strcmp(reason, "BaselineShift")    == 0) return 1;
  if (strcmp(reason, "SuddenFall")       == 0) return 2;
  if (strcmp(reason, "ActivityCollapse") == 0) return 3;
  if (strcmp(reason, "ColicRolling")     == 0) return 4;
  return 0;
}

const char* idxToAlertReason(uint8_t idx) {
  switch (idx) {
    case 1: return "BaselineShift";
    case 2: return "SuddenFall";
    case 3: return "ActivityCollapse";
    case 4: return "ColicRolling";
    default: return nullptr;
  }
}

bool post(String url, String body) {
  if (!ensureWifi()) return false;
  Serial.println("POST " + url);
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
  http.end();
  if (code < 0) { Serial.println("POST failed: " + http.errorToString(code)); return false; }
  Serial.printf("  → %d\n", code);
  return code >= 200 && code < 300;
}

// ── Buffer ────────────────────────────────────────────────────────────────────

void pushToBuffer(HorseState state) {
  if (bufferCount >= MAX_BUFFER) return;
  time_t now; time(&now);
  BufferedReading& r = buffer[bufferCount++];
  r.unixTime        = (int32_t)now;
  r.pitch           = pitch;
  r.roll            = roll;
  r.acceleration    = acceleration;
  r.angularVelocity = angularVelocity;
  r.stillCycles     = (int16_t)stillCycles;
  r.state           = (uint8_t)state;
  r.alertReasonIdx  = alertReasonToIdx(alertReason);
}

void flushBuffer() {
  if (bufferCount == 0) return;
  if (!ensureWifi()) return;

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < bufferCount; i++) {
    const BufferedReading& r = buffer[i];
    JsonObject obj = arr.add<JsonObject>();
    obj["timestamp"]       = isoFromUnix(r.unixTime);
    obj["pitch"]           = r.pitch;
    obj["roll"]            = r.roll;
    obj["acceleration"]    = r.acceleration;
    obj["angularVelocity"] = r.angularVelocity;
    obj["lyingCycles"]     = r.stillCycles;
    obj["state"]           = stateToString((HorseState)r.state);
    obj["activity"]        = r.acceleration > ACCEL_MOVING ? "moving" : "stable";
    const char* reason = idxToAlertReason(r.alertReasonIdx);
    if (reason) obj["alertReason"] = reason;
  }

  String body;
  serializeJson(doc, body);
  String url = String(SERVER_URL) + "/horses/" + HORSE_ID + "/readings/batch";
  if (post(url, body)) {
    Serial.printf("Flushed %d readings\n", bufferCount);
    bufferCount = 0;
  }
}

// ── Config ────────────────────────────────────────────────────────────────────

void fetchConfig() {
  if (!ensureWifi()) return;
  HTTPClient http;
  String url = String(SERVER_URL) + "/horses/" + HORSE_ID + "/config";
  if (url.startsWith("https")) {
    WiFiClientSecure* client = new WiFiClientSecure;
    client->setInsecure();
    http.begin(*client, url);
  } else {
    http.begin(url);
  }
  http.setTimeout(HTTP_TIMEOUT_MS);
  int code = http.GET();
  if (code == 200) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    sleepNormalS = doc["sleepSeconds"]  | sleepNormalS;
    sendEveryN   = doc["sendEveryN"]    | sendEveryN;
    changeAccel  = doc["changeAccel"]   | changeAccel;
    changePitch  = doc["changePitch"]   | changePitch;
    changeRoll   = doc["changeRoll"]    | changeRoll;
    Serial.printf("Config: sleep=%ds  sendEveryN=%d  changeAccel=%.2f  changePitch=%.1f  changeRoll=%.1f\n",
      sleepNormalS, sendEveryN, changeAccel, changePitch, changeRoll);
    if (doc["reboot"] | false) {
      post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/config/reboot/clear", "{}");
      Serial.println("Remote reboot triggered");
      delay(500);
      ESP.restart();
    }
  }
  http.end();
}

// ── Sending ───────────────────────────────────────────────────────────────────

void sendReading(HorseState state) {
  JsonDocument doc;
  doc["timestamp"]       = isoTimestamp();
  doc["pitch"]           = pitch;
  doc["roll"]            = roll;
  doc["acceleration"]    = acceleration;
  doc["activity"]        = acceleration > ACCEL_MOVING ? "moving" : "stable";
  doc["state"]           = stateToString(state);
  doc["angularVelocity"] = angularVelocity;
  doc["lyingCycles"]     = stillCycles;
  if (alertReason)       doc["alertReason"] = alertReason;
  String body; serializeJson(doc, body);
  post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/readings", body);
}

void sendDeviceStatus() {
  JsonDocument doc;
  doc["timestamp"]      = isoTimestamp();
  doc["batteryVoltage"] = batteryVoltage;
  doc["batteryPercent"] = voltageToPercent(batteryVoltage);
  String body; serializeJson(doc, body);
  post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/status", body);
}

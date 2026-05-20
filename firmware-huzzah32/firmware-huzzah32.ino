#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_BNO08x.h>
#include <time.h>
#include "config.h"

#define BATTERY_PIN       A13
#define WIFI_TIMEOUT_MS   15000  // give up connecting after 15s
#define NTP_TIMEOUT_MS    10000  // give up NTP sync after 10s
#define HTTP_TIMEOUT_MS   5000   // HTTP request timeout

enum HorseState { Standing, LyingDown, Moving, Rolling };

WiFiMulti wifiMulti;
Adafruit_BNO08x bno;

// Runtime config — defaults match server-side defaults
float tiltThresholdDegrees = 60.0f;
int   lyingConfirmMs       = 10000;
int   heartbeatMs          = 300000;
int   sampleIntervalMs     = 1000;

// Mounting offset — set during calibration on boot
float       pitchOffset       = 0.0f;
float       rollOffset        = 0.0f;
const char* calibrationResult = "unknown";

bool timeSynced = false;

HorseState    currentState = Standing;
bool          isTilted     = false;
unsigned long tiltStart    = 0;
unsigned long lastSend     = 0;
unsigned long lastSample   = 0;

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  connectWifi();   // non-blocking after timeout
  syncTime();      // skipped if no WiFi
  fetchConfig();   // skipped if no WiFi
  initBno();       // halts only if sensor is missing — hardware must work
  calibrate();
}

// ── WiFi ──────────────────────────────────────────────────────────────────────

bool ensureWifi() {
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  if (wifiMulti.run() == WL_CONNECTED) return true;

  Serial.print("Reconnecting WiFi");
  unsigned long start = millis();
  while (millis() - start < WIFI_TIMEOUT_MS) {
    if (wifiMulti.run() == WL_CONNECTED) {
      Serial.println(" connected to " + WiFi.SSID());
      return true;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println(" failed — continuing offline");
  return false;
}

void disconnectWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi off");
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
    if (now > 100000) {
      timeSynced = true;
      Serial.println(" done");
      return;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println(" failed — timestamps will be inaccurate");
}

// ── BNO085 ────────────────────────────────────────────────────────────────────

void initBno() {
  if (!bno.begin_I2C()) {
    Serial.println("BNO085 not found — check wiring");
    while (1) delay(100);  // sensor is required, cannot run without it
  }
  bno.enableReport(SH2_ROTATION_VECTOR);
  bno.enableReport(SH2_LINEAR_ACCELERATION);
  Serial.println("BNO085 ready");
}

void readBno(float &pitch, float &roll, float &acceleration, String &activity) {
  sh2_SensorValue_t event;
  while (bno.getSensorEvent(&event)) {
    if (event.sensorId == SH2_ROTATION_VECTOR)
      computePitchRoll(event.un.rotationVector, pitch, roll);

    if (event.sensorId == SH2_LINEAR_ACCELERATION) {
      acceleration = sqrt(
        pow(event.un.linearAcceleration.x, 2) +
        pow(event.un.linearAcceleration.y, 2) +
        pow(event.un.linearAcceleration.z, 2)
      );
      activity = acceleration > 0.5f ? "moving" : "stable";
    }
  }
}

void computePitchRoll(sh2_RotationVectorWAcc_t q, float &pitch, float &roll) {
  pitch = atan2(2*(q.real*q.i + q.j*q.k), 1 - 2*(q.i*q.i + q.j*q.j)) * 180.0f / PI;
  roll  = asin (2*(q.real*q.j - q.k*q.i)) * 180.0f / PI;
}

// ── Calibration ───────────────────────────────────────────────────────────────

void calibrate() {
  Serial.println("Calibrating — keep horse still for 10 seconds");

  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
  }

  float pitch = 0, roll = 0, acceleration = 0;
  String activity;
  readBno(pitch, roll, acceleration, activity);

  if (acceleration < 0.3f) {
    pitchOffset = pitch;
    rollOffset  = roll;
    Serial.printf("Calibrated — pitch offset: %.1f, roll offset: %.1f\n", pitchOffset, rollOffset);
    calibrationResult = "success";
  } else {
    Serial.println("Movement detected during calibration — using zero offsets");
    calibrationResult = "failed_movement";
  }

  digitalWrite(LED_BUILTIN, HIGH);
}

// ── State detection ───────────────────────────────────────────────────────────

HorseState detectState(float pitch, float roll, float acceleration) {
  bool tilted = abs(pitch - pitchOffset) > tiltThresholdDegrees
             || abs(roll  - rollOffset)  > tiltThresholdDegrees;
  bool moving = acceleration > 0.5f;

  if (tilted && moving) {
    isTilted = false;
    return Rolling;
  }

  if (tilted) {
    if (!isTilted) {
      isTilted  = true;
      tiltStart = millis();
    }
    return millis() - tiltStart >= (unsigned long)lyingConfirmMs ? LyingDown : currentState;
  }

  isTilted = false;
  return moving ? Moving : Standing;
}

// ── Battery ───────────────────────────────────────────────────────────────────

float readBatteryVoltage() {
  return analogRead(BATTERY_PIN) * 2.0f * 3.3f / 4095.0f;
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
  time_t now;
  time(&now);
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", gmtime(&now));
  return String(buf);
}

const char* stateToString(HorseState state) {
  switch (state) {
    case Standing:  return "Standing";
    case LyingDown: return "LyingDown";
    case Moving:    return "Moving";
    case Rolling:   return "Rolling";
    default:        return "Standing";
  }
}

bool post(String url, String body) {
  if (!ensureWifi()) return false;

  HTTPClient http;
  http.begin(url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  http.end();

  if (code < 0) {
    Serial.println("POST failed: " + http.errorToString(code));
    return false;
  }
  return true;
}

// ── Config ────────────────────────────────────────────────────────────────────

void clearRecalibrate(const char* status) {
  if (!ensureWifi()) return;

  JsonDocument doc;
  doc["status"] = status;
  String body;
  serializeJson(doc, body);
  post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/config/recalibrate/clear", body);
}

void fetchConfig() {
  if (!ensureWifi()) return;

  HTTPClient http;
  http.begin(String(SERVER_URL) + "/horses/" + HORSE_ID + "/config");
  http.setTimeout(HTTP_TIMEOUT_MS);
  int code = http.GET();

  if (code == 200) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    tiltThresholdDegrees = doc["tiltThresholdDegrees"] | tiltThresholdDegrees;
    lyingConfirmMs       = doc["lyingConfirmMs"]       | lyingConfirmMs;
    heartbeatMs          = doc["heartbeatMs"]          | heartbeatMs;
    sampleIntervalMs     = doc["sampleIntervalMs"]     | sampleIntervalMs;
    Serial.println("Config updated");

    if (doc["recalibrate"] | false) {
      calibrate();
      clearRecalibrate(calibrationResult);
    }
  } else if (code < 0) {
    Serial.println("Config fetch failed: " + http.errorToString(code));
  }

  http.end();
}

// ── Sending ───────────────────────────────────────────────────────────────────

void sendReading(float pitch, float roll, float acceleration, String activity, HorseState state) {
  JsonDocument doc;
  doc["timestamp"]    = isoTimestamp();
  doc["pitch"]        = pitch;
  doc["roll"]         = roll;
  doc["acceleration"] = acceleration;
  doc["activity"]     = activity;
  doc["state"]        = stateToString(state);

  String body;
  serializeJson(doc, body);
  post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/readings", body);
}

void sendDeviceStatus(float voltage, int percent) {
  JsonDocument doc;
  doc["timestamp"]      = isoTimestamp();
  doc["batteryVoltage"] = voltage;
  doc["batteryPercent"] = percent;

  String body;
  serializeJson(doc, body);
  post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/status", body);
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
  if (millis() - lastSample < (unsigned long)sampleIntervalMs) return;
  lastSample = millis();

  // Retry time sync if it failed at boot
  if (!timeSynced) syncTime();

  float pitch = 0, roll = 0, acceleration = 0;
  String activity = "stable";
  readBno(pitch, roll, acceleration, activity);

  HorseState newState = detectState(pitch, roll, acceleration);

  bool stateChanged = newState != currentState;
  bool heartbeat    = millis() - lastSend >= (unsigned long)heartbeatMs;

  if (stateChanged || heartbeat) {
    currentState = newState;
    lastSend     = millis();

    float voltage = readBatteryVoltage();
    int   percent = voltageToPercent(voltage);

    sendReading(pitch, roll, acceleration, activity, newState);
    sendDeviceStatus(voltage, percent);
    fetchConfig();
    disconnectWifi();
  }
}

#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_BNO08x.h>
#include <time.h>
#include "config.h"

#define BATTERY_PIN      A13
#define SLEEP_SECONDS    30
#define SLEEP_US         (SLEEP_SECONDS * 1000000ULL)
#define WIFI_TIMEOUT_MS  15000
#define NTP_TIMEOUT_MS   10000
#define HTTP_TIMEOUT_MS  5000

enum HorseState { Standing, LyingDown, Moving, Rolling };

// ── RTC memory — survives deep sleep ──────────────────────────────────────────

RTC_DATA_ATTR bool       firstBoot            = true;
RTC_DATA_ATTR float      refQw = 1, refQx = 0, refQy = 0, refQz = 0;
RTC_DATA_ATTR HorseState currentState         = Standing;
RTC_DATA_ATTR int        tiltCycles           = 0;   // consecutive cycles tilted
RTC_DATA_ATTR int        cyclesSinceLastSend  = 0;
RTC_DATA_ATTR bool       timeSynced           = false;
RTC_DATA_ATTR const char* calibrationResult   = "unknown";

// Config — stored in RTC so firmware doesn't need to fetch every wake
RTC_DATA_ATTR float tiltThresholdDegrees = 60.0f;
RTC_DATA_ATTR int   lyingConfirmCycles   = 1;    // recomputed from lyingConfirmMs
RTC_DATA_ATTR int   heartbeatCycles      = 10;   // recomputed from heartbeatMs

// ── Runtime (reset each wake) ──────────────────────────────────────────────────

WiFiMulti wifiMulti;
Adafruit_BNO08x bno;

float curQw = 1, curQx = 0, curQy = 0, curQz = 0;
float pitch = 0, roll = 0, acceleration = 0;
String activity = "stable";
float batteryVoltage = 0;  // read before WiFi to avoid load-sag

// ── Setup — runs once per wake cycle ──────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  batteryVoltage = readBatteryVoltage();  // measure before WiFi draws current

  initBno();

  if (firstBoot) {
    connectWifi();
    syncTime();
    fetchConfig();
    calibrate();
    firstBoot = false;
  }

  readSensor();

  HorseState newState = detectState();
  bool stateChanged   = newState != currentState;
  cyclesSinceLastSend++;
  bool heartbeat = cyclesSinceLastSend >= heartbeatCycles;

  Serial.printf("pitch: %5.1f  roll: %5.1f  accel: %.2f  tilt: %.1f°  state: %s  battery: %.2fV (%d%%)\n",
    pitch, roll, acceleration, tiltAngleDeg(), stateToString(newState), batteryVoltage, voltageToPercent(batteryVoltage));

  if (stateChanged || heartbeat) {
    currentState        = newState;
    cyclesSinceLastSend = 0;
    connectWifi();
    if (!timeSynced) syncTime();
    sendReading(newState);
    sendDeviceStatus();
    fetchConfig();
    disconnectWifi();
  }

  Serial.printf("Sleeping %d seconds...\n", SLEEP_SECONDS);
  esp_sleep_enable_timer_wakeup(SLEEP_US);
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
  delay(200);  // let sensor produce its first fresh sample after wake
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
        activity = acceleration > 0.5f ? "moving" : "stable";
        gotAccel = true;
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

// ── Calibration ───────────────────────────────────────────────────────────────

void calibrate() {
  Serial.println("Calibrating — keep horse still for 10 seconds");
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_BUILTIN, HIGH); delay(500);
    digitalWrite(LED_BUILTIN, LOW);  delay(500);
  }
  readSensor();
  if (acceleration < 0.3f) {
    refQw = curQw; refQx = curQx; refQy = curQy; refQz = curQz;
    Serial.printf("Calibrated — pitch: %.1f, roll: %.1f\n", pitch, roll);
    calibrationResult = "success";
  } else {
    Serial.println("Movement detected — keeping previous reference");
    calibrationResult = "failed_movement";
  }
  digitalWrite(LED_BUILTIN, HIGH);
}

// ── State detection ───────────────────────────────────────────────────────────

float tiltAngleDeg() {
  float dot = refQw*curQw + refQx*curQx + refQy*curQy + refQz*curQz;
  if (dot < 0) dot = -dot;
  if (dot > 1.0f) dot = 1.0f;
  return 2.0f * acos(dot) * 180.0f / PI;
}

HorseState detectState() {
  bool tilted = tiltAngleDeg() > tiltThresholdDegrees;
  bool moving = acceleration > 0.5f;

  if (tilted && moving) { tiltCycles = 0; return Rolling; }

  if (tilted) {
    tiltCycles++;
    return tiltCycles >= lyingConfirmCycles ? LyingDown : currentState;
  }

  tiltCycles = 0;
  return moving ? Moving : Standing;
}

// ── Battery ───────────────────────────────────────────────────────────────────

float readBatteryVoltage() {
  // analogReadMilliVolts uses ESP32 factory ADC calibration — more accurate than raw analogRead
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

const char* stateToString(HorseState s) {
  switch (s) {
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
  if (code < 0) { Serial.println("POST failed: " + http.errorToString(code)); return false; }
  return true;
}

// ── Config ────────────────────────────────────────────────────────────────────

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
    int lyingConfirmMs   = doc["lyingConfirmMs"]       | 10000;
    int heartbeatMs      = doc["heartbeatMs"]          | 300000;
    lyingConfirmCycles   = max(1, lyingConfirmMs   / (SLEEP_SECONDS * 1000));
    heartbeatCycles      = max(1, heartbeatMs      / (SLEEP_SECONDS * 1000));
    Serial.printf("Config: threshold=%.0f°  lyingCycles=%d  heartbeatCycles=%d\n",
      tiltThresholdDegrees, lyingConfirmCycles, heartbeatCycles);
    if (doc["recalibrate"] | false) {
      calibrate();
      JsonDocument r; r["status"] = calibrationResult;
      String body; serializeJson(r, body);
      post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/config/recalibrate/clear", body);
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

// ── Sending ───────────────────────────────────────────────────────────────────

void sendReading(HorseState state) {
  JsonDocument doc;
  doc["timestamp"]    = isoTimestamp();
  doc["pitch"]        = pitch;
  doc["roll"]         = roll;
  doc["tiltDeg"]      = tiltAngleDeg();
  doc["acceleration"] = acceleration;
  doc["activity"]     = activity;
  doc["state"]        = stateToString(state);
  String body; serializeJson(doc, body);
  post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/readings", body);
}

void sendDeviceStatus() {
  float refPitch = atan2(2*(refQw*refQx + refQy*refQz), 1 - 2*(refQx*refQx + refQy*refQy)) * 180.0f / PI;
  float refRoll  = asin(constrain(2*(refQw*refQy - refQz*refQx), -1.0f, 1.0f)) * 180.0f / PI;
  JsonDocument doc;
  doc["timestamp"]      = isoTimestamp();
  doc["batteryVoltage"] = batteryVoltage;
  doc["batteryPercent"] = voltageToPercent(batteryVoltage);
  doc["pitchRef"]       = refPitch;
  doc["rollRef"]        = refRoll;
  String body; serializeJson(doc, body);
  post(String(SERVER_URL) + "/horses/" + HORSE_ID + "/status", body);
}

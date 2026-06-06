#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <esp_sleep.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define BNO_INT_PIN 14
#define WIFI_TIMEOUT_MS 15000
#define SERVER_URL "https://horse-collar.sandro-raess.ch/api"
#define HORSE_ID "6a120cd3377aa365b26a8cf8"

// -------------------- WIFI --------------------

const char* wifiNetworks[][2] = {
  { "RUT241_C041", "Xf9u1H2E" },
  { "Stop Animal Cruelty - Go Vegan", "Rmt4ypnnjN7vcxnh" }
};

WiFiMulti wifiMulti;

// -------------------- QUATERNION --------------------

struct Quaternion {
  float x, y, z, w;
};

Quaternion conjugate(const Quaternion& q) {
  return { -q.x, -q.y, -q.z, q.w };
}

Quaternion multiply(const Quaternion& a, const Quaternion& b) {
  Quaternion r;
  r.w = a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z;
  r.x = a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y;
  r.y = a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x;
  r.z = a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w;
  return r;
}

// -------------------- SENSOR MODEL --------------------

class SensorReading {
public:
  unsigned long timestamp;
  float qx, qy, qz, qw;
  float accuracy;

  SensorReading()
    : timestamp(0), qx(0), qy(0), qz(0), qw(1), accuracy(0) {}

  SensorReading(unsigned long ts, float x, float y, float z, float w, float acc)
    : timestamp(ts), qx(x), qy(y), qz(z), qw(w), accuracy(acc) {}

  String toJson() const {
    DynamicJsonDocument doc(256);
    doc["timestamp"] = timestamp;
    doc["qx"] = qx;
    doc["qy"] = qy;
    doc["qz"] = qz;
    doc["qw"] = qw;
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

bool isCalibrated = false;
SensorReading referenceSensorReading;

enum Posture { STANDING, LYING };
Posture posture = STANDING;

const float LYING_THRESHOLD = 75.0f;
const float STANDING_THRESHOLD = 45.0f;

uint32_t SH2_ROTATION_VECTOR_INTERVAL = 5000000;

bool postureChanged = false;

// -------------------- ISR --------------------

void IRAM_ATTR bnoISR() {
  newDataReady = true;
}

// -------------------- FORWARD DECLARATIONS --------------------

void calibrate(const SensorReading& currentReading);
SensorReading getAdjustedReading(const SensorReading& r);
bool ensureWifi();
void connectWifi();
void disconnectWifi();
void sendPostureChange();
Posture detectPosture(float gz);

// -------------------- SETUP --------------------

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin();

  if (!bno08x.begin_I2C()) {
    Serial.println("BNO085 not found");
    while (1) delay(10);
  }

  bno08x.enableReport(SH2_ROTATION_VECTOR, SH2_ROTATION_VECTOR_INTERVAL);

  pinMode(BNO_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BNO_INT_PIN), bnoISR, FALLING);

  esp_sleep_enable_ext0_wakeup((gpio_num_t)BNO_INT_PIN, 0);

  for (auto& n : wifiNetworks) {
    wifiMulti.addAP(n[0], n[1]);
  }

  Serial.println("Ready.");
}

// -------------------- CALIBRATION --------------------

void calibrate(const SensorReading& currentReading) {
  referenceSensorReading = currentReading;
  isCalibrated = true;
  Serial.println("Calibrated");
}

// -------------------- SENSOR ADJUSTMENT --------------------

SensorReading getAdjustedReading(const SensorReading& r) {
  if (!isCalibrated) return r;

  Quaternion current = { r.qx, r.qy, r.qz, r.qw };
  Quaternion reference = {
    referenceSensorReading.qx,
    referenceSensorReading.qy,
    referenceSensorReading.qz,
    referenceSensorReading.qw
  };

  Quaternion adjusted = multiply(conjugate(reference), current);

  return SensorReading(
    r.timestamp,
    adjusted.x, adjusted.y, adjusted.z, adjusted.w,
    r.accuracy
  );
}

// -------------------- WIFI --------------------

void connectWifi() {
  ensureWifi();
}

bool ensureWifi() {
  if (WiFi.getMode() == WIFI_OFF)
    WiFi.mode(WIFI_STA);

  if (wifiMulti.run() == WL_CONNECTED)
    return true;

  unsigned long start = millis();

  while (millis() - start < WIFI_TIMEOUT_MS) {
    if (wifiMulti.run() == WL_CONNECTED) {
      Serial.println("WiFi connected: " + WiFi.SSID());
      return true;
    }
    delay(500);
  }

  Serial.println("WiFi failed");
  return false;
}

void disconnectWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// -------------------- NETWORK SEND --------------------

void sendPostureChange() {
  WiFiClientSecure client;
  client.setInsecure();

  //String url = String(SERVER_URL) + "/horses/" + HORSE_ID + "/posture-change";
  String url = String(SERVER_URL) + "/horses/" + HORSE_ID + "/readings/posture-change";

  HTTPClient http;
  http.begin(client, url);

  int code = http.GET();

  Serial.print("Response: ");
  Serial.println(code);

  http.end();
}

// -------------------- LOOP --------------------

void loop() {

  if (postureChanged) {
    postureChanged = false;
    connectWifi();
    sendPostureChange();
    disconnectWifi();
  }

  bool hasData;

  noInterrupts();
  hasData = newDataReady;
  newDataReady = false;
  interrupts();

  if (!hasData) {
    Serial.println("Light sleep start");
    esp_light_sleep_start();
    return;
  }

  while (bno08x.getSensorEvent(&event)) {

    if (event.sensorId == SH2_ROTATION_VECTOR) {

      float qi = event.un.rotationVector.i;
      float qj = event.un.rotationVector.j;
      float qk = event.un.rotationVector.k;
      float qr = event.un.rotationVector.real;

      SensorReading reading(millis(), qi, qj, qk, qr, 0);

      if (!isCalibrated) {
        calibrate(reading);
        continue;
      }

      SensorReading adjusted = getAdjustedReading(reading);

      float qw = adjusted.qw;
      float qx = adjusted.qx;
      float qy = adjusted.qy;
      float qz = adjusted.qz;

      float gz = qw*qw - qx*qx - qy*qy + qz*qz;

      posture = detectPosture(gz);
    }
  }
}

// -------------------- POSTURE DETECTION --------------------

Posture detectPosture(float gz) {
  switch (posture) {
    case STANDING:
      if (gz < cos(LYING_THRESHOLD * PI / 180.0)) {
        Serial.println(">> LYING");
        postureChanged = true;
        return LYING;
      }
      break;
    case LYING:
      if (gz > cos(STANDING_THRESHOLD * PI / 180.0)) {
        Serial.println(">> STANDING");
        postureChanged = true;
        return STANDING;
      }
      break;
  }
  return posture;
}
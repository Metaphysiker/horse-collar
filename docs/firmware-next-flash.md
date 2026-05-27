# Planned Firmware Changes (next flash)

## 1. Send quaternion instead of Euler angles

Currently, the firmware converts the BNO085 rotation vector to pitch/roll on-device and discards the quaternion. This causes:
- Gimbal lock (pitch flips to ±127° when roll exceeds 90°)
- Loss of yaw entirely
- Server-side horse roll approximation is inaccurate at high angles

**Change:** Send `qw`, `qx`, `qy`, `qz` raw. Keep pitch/roll alongside for backward compatibility during transition.

```cpp
doc["qw"] = curQw;
doc["qx"] = curQx;
doc["qy"] = curQy;
doc["qz"] = curQz;
```

Server and frontend then compute pitch, roll, yaw, and horse roll from quaternion — no gimbal lock, correct at all angles.

---

## 2. Send Unix timestamp as integer

Currently, `isoFromUnix()` converts `time_t` to an ISO string in firmware. This is unnecessary and error-prone (TZ confusion).

**Change:** Send the raw Unix timestamp directly.

```cpp
// instead of:
obj["timestamp"] = isoFromUnix(r.unixTime);
// send:
obj["timestamp"] = r.unixTime;
```

Server parses with `DateTimeOffset.FromUnixTimeSeconds(v).UtcDateTime`. No conversion, no formatting, always UTC.

---

## 3. Add yaw (compass heading)

`SH2_ROTATION_VECTOR` already uses the magnetometer and is already enabled — yaw costs zero extra sensor reads, just one extra `atan2` computation.

**Change:** Extract yaw from the existing quaternion read.

```cpp
float yaw = atan2(2*(curQw*curQz + curQx*curQy), 1 - 2*(curQy*curQy + curQz*curQz)) * 180.0f / PI;
```

Send as `yaw` field. Useful for detecting which direction the horse is facing and long-term movement patterns.

---

## 4. Re-sync NTP on every WiFi connection

The ESP32 HUZZAH32 has no external 32.768 kHz crystal — it uses an internal 150 kHz RC oscillator which is temperature-sensitive and drifts significantly (~10 min per 36 h observed). NTP is the only source of accurate time.

Currently `timeSynced` is `RTC_DATA_ATTR` — NTP syncs once at hard boot and never again.

Since WiFi is already connected for every batch upload, re-syncing NTP costs almost nothing (one UDP packet). Just remove the `timeSynced` guard so `syncTime()` is called every upload cycle.

**Change:** Remove `RTC_DATA_ATTR bool timeSynced` and always call `syncTime()` when WiFi is up.

---

## 5. Fix timezone offset

`TZ_OFFSET = 3600` (UTC+1, CET) is wrong in summer — Switzerland is CEST (UTC+2 = 7200). Since timestamps use `gmtime()` this is harmless for stored data, but fix for correctness and any local-time logging.

**Change in `config.*.h`:**
```cpp
// Option A: hardcode CEST
#define TZ_OFFSET 7200

// Option B: use POSIX timezone string (handles DST automatically)
configTime(0, 0, NTP_SERVER);
setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
tzset();
```

Option B is correct year-round.

---

## 6. Additional sensor data (low impact)

These use already-enabled reports or fast additional reads:

| Field | Source | Cost |
|---|---|---|
| `linearAccelX/Y/Z` | `SH2_LINEAR_ACCELERATION` (already enabled) | free — just send components instead of magnitude |
| `gyroX/Y/Z` | `SH2_GYROSCOPE_CALIBRATED` (already enabled) | free — just send components instead of magnitude |
| `gravityX/Y/Z` | `SH2_GRAVITY` | one extra report enable + poll |
| `temperature` | `SH2_TEMPERATURE` | one extra report enable + poll |

Sending raw X/Y/Z components instead of magnitudes gives the server more information to work with (direction of movement, not just intensity).

---

## Summary of JSON payload change

**Current:**
```json
{
  "timestamp": "2026-05-26T17:44:52",
  "pitch": -8.3, "roll": -7.7,
  "acceleration": 0.11, "angularVelocity": 0.03,
  "temperature": 25.0, "state": "Standing", "alertReason": null
}
```

**After:**
```json
{
  "timestamp": 1748283892,
  "qw": 0.997, "qx": -0.042, "qy": -0.031, "qz": 0.012,
  "pitch": -8.3, "roll": -7.7, "yaw": 142.5,
  "acceleration": 0.11, "angularVelocity": 0.03,
  "linearAccelX": 0.08, "linearAccelY": -0.07, "linearAccelZ": 0.03,
  "gyroX": 0.01, "gyroY": -0.02, "gyroZ": 0.01,
  "temperature": 25.0, "state": "Standing", "alertReason": null
}
```

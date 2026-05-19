# Horse Collar Monitoring System

Detects horse activity (standing, lying, rolling) and sends alerts for potential colic or distress. Device mounts on a chest belt.

---

## Current Status (2026-05-19)

Hardware ordered from BerryBase, delivery expected 2026-05-19 to 2026-05-21. Nothing built yet.

### Key constraints
- User has **no soldering tools** — all connections must be solderless
- User has a **personal server** for data logging
- Single stable box with some outside space

### Next steps
1. Receive hardware from BerryBase
2. Order weatherproof enclosure from reichelt.de or conrad.ch
3. Order UWB modules from AliExpress (`Makerfabs ESP32 UWB DW1000` x3)
4. Order mini breadboard (~170 pin) to hold boards together
5. Wire up BNO085 to HUZZAH32 via Qwiic cable
6. Set up Arduino IDE and flash firmware
7. Log baseline data for 1 week before building alerts
8. Set up Twilio voice call alerts

---

## Hardware

### Ordered from BerryBase

| Item | Product Nr | Price |
|---|---|---|
| Adafruit BNO085 IMU Breakout | ADA4754 | - |
| Adafruit HUZZAH32 ESP32 Feather (with stacking headers) | ADA3619 | CHF 21.95 |
| LiPo Battery 3.7V 2500mAh JST | LP-785060-J | - |
| 40pin Jumper Female-Female | - | - |
| Soldered easyC/Qwiic to Dupont Female cable | SOL-333314 | - |

### Still needed
- Weatherproof enclosure ~80x65x40mm (IP65/67) — order from reichelt.de or conrad.ch
- UWB modules for height detection (order from AliExpress): `Makerfabs ESP32 UWB DW1000` x3

---

## Wiring

### BNO085 → HUZZAH32
Via Qwiic/STEMMA QT to female jumper wire cable (SOL-333314):

| BNO085 | HUZZAH32 |
|---|---|
| VCC | 3V |
| GND | GND |
| SDA | SDA |
| SCL | SCL |

### Battery → HUZZAH32
JST connector plugs directly into HUZZAH32 JST socket. Charge via Micro-USB into HUZZAH32.

---

## Enclosure

- Target size: ~80x65x40mm
- Must be IP65/67 (weatherproof — horse sweats, rain)
- Fix components inside with double-sided velcro + polyethylene foam padding
- Use small cable tie on Qwiic cable to prevent connector strain
- Use mini breadboard (~170 pin) to hold HUZZAH32 and BNO085 together as one unit

---

## Detection Logic

### Sampling rate
- Read BNO085 every **1 second**
- Send data to server only on **state change** + heartbeat every **5 minutes**

### States
| State | Condition |
|---|---|
| Standing | Stable orientation, pitch/roll ~0° |
| Lying down | Pitch or roll > 60° for more than 10 seconds |
| Moving | Activity detected, upright orientation |
| Rolling (colic risk) | Activity detected + tilted orientation |

### BNO085 data to use
- **Gravity vector** — changes clearly when horse lies down
- **Absolute orientation / rotation vector** — pitch and roll angles
- **Activity classification** — stable, moving etc.
- **Linear acceleration** — sudden movements

### Battery monitoring
Read battery voltage via pin A13 on HUZZAH32:

| Voltage | Charge |
|---|---|
| 4.2V | 100% |
| 3.9V | ~60% |
| 3.7V | ~30% |
| 3.4V | ~5% |

---

## Data Logging (Server)

Log everything first to establish baseline — learn what normal looks like for this specific horse before building alerts.

### Payload (every state change)
```json
{
  "timestamp": "2026-05-19T08:00:00",
  "pitch": 12.3,
  "roll": 2.1,
  "activity": "stable",
  "acceleration": 0.12,
  "battery_voltage": 3.9,
  "battery_percent": 60,
  "state": "standing"
}
```

---

## Alerts (Twilio)

Voice call to mobile when abnormal state detected.

| Destination | Cost |
|---|---|
| Swiss mobile | ~$0.05 per call |
| Swiss landline | ~$0.01 per call |
| Twilio number | $1.15/month |

Trigger alert when:
- Horse rolling for more than 30 seconds (colic risk)
- Horse lying down for unusually long time
- Battery below 20%

---

## Future: UWB Height Detection

3x `Makerfabs ESP32 UWB DW1000` modules (~€60 total from AliExpress):
- Anchor 1: high (stable roof edge) — covers indoor + outdoor
- Anchor 2: low (stable wall, ground level)
- Tag: horse collar

Detects absolute height → confirms lying down independently of IMU orientation.
Combined with BNO085 gives much richer picture (lying + rolling = strong colic indicator).

# Battery Life Estimates

## Hardware

- **Board:** Adafruit HUZZAH32 (ESP32)
- **Sensor:** BNO085 (~1.1 mA active)
- **Battery:** 2500 mAh LiPo
- **Charger:** built into HUZZAH32, charges at ~200 mA via USB-C

---

## Power consumption

| Mode | Current draw |
|---|---|
| ESP32 deep sleep | ~0.01 mA |
| ESP32 active, WiFi off (sensor read) | ~80 mA |
| ESP32 active, WiFi transmitting | ~180 mA |

---

## Current setup — deep sleep with batch sending

The firmware deep sleeps between readings and only connects to WiFi every `sendEveryN` cycles to upload a batch. WiFi connect + send + disconnect takes ~15 seconds.

**Assumptions:**
- Sensor read per wake: ~500 ms at 80 mA
- WiFi per batch: ~15 s at 180 mA
- `sendEveryN = 60`

| Sleep interval | Upload every | Avg current | Est. battery life (2500 mAh) |
|---|---|---|---|
| 1s | ~60s | ~68 mA | **~37 hours** |
| 2s | ~2 min | ~38 mA | **~66 hours (~2.7 days)** |
| 5s | ~5 min | ~16 mA | **~6.5 days** |
| 10s | ~10 min | ~8 mA | **~13 days** |

---

## Trade-offs

- **1s** — maximum data density, good for active monitoring or data collection sessions
- **2s** — good balance for daily use, ~2.5 days between charges
- **5s** — field deployment, weekly charging
- **10s** — long-term unattended monitoring, charge every ~2 weeks

Alert states always send immediately regardless of interval — battery impact is minimal since alerts are rare.

---

## Notes

- Real-world battery capacity is typically 80–90% of rated value — reduce estimates accordingly.
- WiFi connection time varies by signal strength. Poor signal = longer connect time = faster drain.
- The HUZZAH32 battery percentage reading is approximate (±5–10%).

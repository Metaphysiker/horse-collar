# Battery Life Estimates

## Hardware

- **Board:** Adafruit HUZZAH32 (ESP32)
- **Sensor:** BNO085 (~1.1 mA active)
- **Charger:** built into HUZZAH32, charges at ~200 mA via USB-C

---

## Power consumption

| Mode | Current draw |
|---|---|
| ESP32 active, WiFi transmitting | ~200–250 mA peak |
| ESP32 active, WiFi connected idle | ~100–130 mA |
| ESP32 active, WiFi off | ~70–90 mA |
| BNO085 active | ~1.1 mA |

---

## Scenario A — WiFi always on

WiFi stays associated to the network continuously, even between readings.

**Average draw:** ~110 mA

| Battery capacity | Estimated life |
|---|---|
| 500 mAh | ~4.5 hours |
| 1 000 mAh | ~9 hours |
| 2 000 mAh | ~18 hours |
| **2 500 mAh (ours)** | **~22 hours** |

Not practical for field use.

---

## Scenario B — Current setup (WiFi off between heartbeats)

WiFi is turned off after each send and only reconnects on the next heartbeat (every 5 minutes) or state change. Estimated WiFi-on time is ~15–20 seconds per cycle.

**Duty cycle:** ~6% WiFi on, ~94% WiFi off  
**Average draw:** ~80 mA

| Battery capacity | Estimated life |
|---|---|
| 500 mAh | ~6 hours |
| 1 000 mAh | ~12 hours |
| 2 000 mAh | ~25 hours |
| **2 500 mAh (ours)** | **~31 hours** |

---

## Scenario C — Deep sleep (not yet implemented)

Between heartbeats the ESP32 enters deep sleep (~0.01 mA). The BNO085 would need to be woken via interrupt or the ESP32 would need to wake on a timer to sample.

**Average draw:** ~5–10 mA (estimate)

| Battery capacity | Estimated life |
|---|---|
| 500 mAh | ~2.5 days |
| 1 000 mAh | ~5 days |
| 2 000 mAh | ~10 days |
| **2 500 mAh (ours)** | **~12 days** |

This would require significant firmware changes but is the right path for a real deployment.

---

## Notes

- Real-world battery capacity is typically 80–90% of the rated value.
- WiFi connection time varies — if the network takes longer to associate, battery drains faster.
- Lower heartbeat frequency (e.g. 10 min instead of 5 min) meaningfully extends Scenario B life.
- The HUZZAH32 battery percentage reading is approximate (±5–10%).

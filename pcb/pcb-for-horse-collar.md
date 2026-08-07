| Power Switch** | Small Slide Switch | A physical switch to disconnect the battery from the rest of the circuit. |
| **Connectors** | USB-C, JST-PH (2-pin) | USB-C for charging/programming. JST for the LiPo battery connection. |

## 3. Block Diagram

This diagram illustrates the high-level architecture of the custom PCB, showing power and data pathways.

```mermaid
graph TD
    subgraph Power
        BATT[LiPo Battery] --> JST[JST Connector]
        JST --> SW[On/Off Switch]
        SW --> LDO[3.3V LDO Regulator]
        SW --> CHG[LiPo Charger IC]
    end

    subgraph USB
        USB[USB-C Port] --> CHG
        USB --> UART[USB-UART Bridge]
    end

    subgraph Core
        LDO --> ESP32[ESP32-WROOM-32]
        LDO --> BNO085[BNO085 IMU]
        ESP32 -- I2C (SDA/SCL) --> BNO085
        ESP32 -- UART (TX/RX) --> UART
    end
```

## 4. Design Considerations

### Board Layout
- **Size:** Aim for a rectangular board roughly the size of the BNO085 breakout board, but slightly longer to accommodate the ESP32 module. A target of **~45mm x 25mm** seems feasible.
- The USB-UART bridge needs to be connected to the ESP32's `EN` (Enable) and `GPIO0` pins via a small transistor circuit to enable automatic bootloader mode for flashing. This circuit is standard on all ESP32 development boards and is crucial for a good user experience.

## 5. Next Steps

1.  **Schematic Design:** Create the circuit diagram in an EDA tool (KiCad, Eagle, EasyEDA).
2.  **PCB Layout:** Design the physical board layout based on the schematic.


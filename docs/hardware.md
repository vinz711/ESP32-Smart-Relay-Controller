# Hardware

## Validated 4-Channel Hardware Baseline

The current 4-channel controller baseline uses:

- ESP32 development board
- 4-channel ESP32-compatible active-LOW relay board
- Suitable 5 V power supply for the controller/relay board
- Appropriate enclosure and electrical protection
- Load-side wiring and connectors rated for the intended application

### Relay GPIO Mapping

| Relay | ESP32 GPIO |
|---|---:|
| Relay 1 | GPIO 5 |
| Relay 2 | GPIO 17 |
| Relay 3 | GPIO 16 |
| Relay 4 | GPIO 4 |

### Relay Logic

The validated relay board uses active-LOW control:

| ESP32 GPIO | Relay |
|---|---|
| LOW | ON |
| HIGH | OFF |

### Relay Board Reference

![ESP32 Smart Relay Controller — 4-channel relay hardware](esp32-4-channel-relay.jpg)

The 8-channel relay hardware is maintained separately on the v3.3.0 development line and uses a different GPIO mapping. It is not part of the v3.2.9 4-channel baseline.

## Compatibility

The long-term goal is to support multiple ESP32 boards and relay configurations through hardware-specific configuration rather than application-specific firmware.

## Safety

Relay control may involve mains voltage. Verify the rating of the complete hardware assembly, wiring, connectors, enclosure, and protection devices before connecting a load. Do not work on energized mains circuits. The software power-rating field is an estimation input, not an electrical safety or relay-rating check.

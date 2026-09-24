# Hardware

## Initial Reference Hardware

- ESP32 development board
- 4-channel ESP32-compatible relay board
- Suitable 5 V power supply for the controller/relay board
- Appropriate enclosure and electrical protection

The exact relay-board model and GPIO mapping will be documented with the tested firmware release.

## Compatibility

The long-term goal is to support multiple ESP32 boards and relay configurations through hardware-specific configuration rather than application-specific firmware.

## Safety

Relay control may involve mains voltage. Verify the rating of the complete hardware assembly, wiring, connectors, enclosure, and protection devices before connecting a load. Do not work on energized mains circuits.

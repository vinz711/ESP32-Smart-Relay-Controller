# Firmware v3.2.9 — 4-Relay Smart Management

This directory is reserved for the v3.2.9 4-channel Smart Management firmware baseline.

## Hardware

- ESP32 development board
- 4-channel active-LOW relay board
- Relay 1 → GPIO 5
- Relay 2 → GPIO 17
- Relay 3 → GPIO 16
- Relay 4 → GPIO 4

## Firmware features

- Smart Management responsive web UI
- AUTO / MANUAL modes
- Up to 6 schedules per relay
- Weekday and overnight schedules
- Temporary manual control
- Feeding / Maintenance pause
- Emergency OFF / Resume
- Runtime and estimated power/energy tracking
- Persistent activity logging
- Configurable relay names/icons
- Wi-Fi configuration
- Backup/restore
- Arduino OTA
- Light/dark themes

The firmware source for this version must remain sanitized: do not commit personal Wi-Fi credentials or other secrets.

**Validation status:** development/hardware-validation baseline. v3.2.8 remains the validated stable release until v3.2.9 completes CI and physical hardware validation.

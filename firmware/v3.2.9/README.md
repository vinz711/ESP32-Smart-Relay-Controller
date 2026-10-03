# ESP32 Smart Relay Controller — v3.2.9

## Overview

v3.2.9 is the updated 4-channel Smart Management firmware baseline for the ESP32 Smart Relay Controller.

This version retains the validated 4-channel relay architecture and GPIO mapping while providing the updated Smart Management dashboard and control features.

## Hardware

- ESP32 development board
- 4-channel active-LOW relay board
- 5V relay supply
- Wi-Fi network
- Connected electrical loads as required

## Relay GPIO Mapping

| Relay | ESP32 GPIO |
|---|---:|
| Relay 1 | GPIO 5 |
| Relay 2 | GPIO 17 |
| Relay 3 | GPIO 16 |
| Relay 4 | GPIO 4 |

### Relay Logic

The relay board uses active-LOW control.

| GPIO State | Relay |
|---|---|
| LOW | ON |
| HIGH | OFF |

## Features

- 4 independent relay channels
- AUTO / MANUAL modes
- Multiple schedules per relay
- Weekday scheduling
- Overnight schedules
- Temporary manual control
- Individual Emergency OFF / Resume
- Emergency ALL OFF / Resume ALL
- Feeding / Maintenance pause
- Runtime tracking
- Persistent activity logging
- Per-relay power configuration
- Estimated power and energy usage
- Device names and icons
- Responsive desktop and mobile UI
- Light and dark themes
- Wi-Fi configuration
- Backup / Restore
- Factory Reset
- Arduino OTA
- mDNS support
- Safe relay initialization

## Validation

The firmware should be validated on the physical 4-channel relay hardware for:

- Relay operation
- GPIO mapping
- Manual ON/OFF
- AUTO mode
- Multiple schedules
- Weekday scheduling
- Overnight scheduling
- Temporary manual control
- Emergency OFF / Resume
- Feeding / Maintenance mode
- Runtime tracking
- Power estimation
- Activity logging
- Wi-Fi configuration
- Backup / Restore
- OTA
- Desktop UI
- Mobile UI
- Light / Dark themes

## Version

**Firmware:** v3.2.9

**Hardware:** ESP32 + 4-channel active-LOW relay board
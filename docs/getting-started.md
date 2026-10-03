# Getting Started

This guide helps you set up the ESP32 Smart Relay Controller and prepare the device for first use.

## Project Goal

The ESP32 Smart Relay Controller is designed as a reusable Wi-Fi relay-control platform for applications such as:

- Aquarium automation
- Terrarium automation
- Greenhouse and plant automation
- Irrigation and pump control
- Home automation
- General relay-controlled devices

## Current Baselines

- **v3.2.8** — validated stable 4-channel hardware baseline.
- **v3.2.9** — updated 4-channel Smart Management development/hardware-validation baseline.

Use v3.2.8 when you need the currently validated stable baseline. Use v3.2.9 when validating the updated Smart Management UI and firmware changes.

## Initial Setup

1. Install Arduino IDE.
2. Install ESP32 board support.
3. Connect the ESP32 controller to the computer.
4. Open the firmware project under `firmware/v3.2.9/` when validating the current development baseline, or `firmware/v3.2.8/` for the stable baseline.
5. Select the appropriate ESP32 board and serial port.
6. Configure device settings locally; do not commit personal Wi-Fi credentials or other secrets.
7. Upload the firmware using USB for the initial installation.
8. Connect to the controller web interface using the ESP32 IP address or mDNS hostname when available.
9. Configure relay names/icons, power ratings, schedules, weekdays, and operating modes.
10. After the initial installation is working, OTA can be used for supported firmware updates.

## Validated 4-Channel Relay Mapping

| Relay | GPIO |
|---:|---:|
| 1 | GPIO 5 |
| 2 | GPIO 17 |
| 3 | GPIO 16 |
| 4 | GPIO 4 |

The supported 4-channel relay board uses active-LOW control:

- `LOW` = Relay ON
- `HIGH` = Relay OFF

## First Validation

Before connecting application loads, verify the four relay outputs with the relay board disconnected from hazardous loads where practical.

For v3.2.9, validate:

- Manual ON/OFF for all four relays
- AUTO scheduling
- Multiple schedules
- Weekday and overnight scheduling
- Temporary manual control
- Emergency OFF / Resume
- Emergency ALL OFF / Resume ALL
- Feeding / Maintenance pause
- Runtime and power estimation
- Wi-Fi configuration
- Backup/restore
- OTA
- Desktop and mobile UI

## Safety

This project can control mains-powered equipment. Use an appropriate enclosure, wiring, protection, isolation, and correctly rated components. Do not work on energized mains wiring.

## Related Documentation

- [Hardware](hardware.md)
- [Wiring](wiring.md)
- [Configuration](configuration.md)
- [Web UI](web-ui.md)
- [Scheduler](scheduler.md)
- [OTA Updates](ota-update.md)
- [Troubleshooting](troubleshooting.md)

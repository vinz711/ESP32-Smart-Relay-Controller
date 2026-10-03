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

- **v3.2.9** — current stable 4-channel Smart Management release.
- **v3.2.8** — previous validated stable 4-channel baseline.
- **v3.3.0** — separate 8-channel development line.

Use v3.2.9 for the current stable 4-channel controller. Use the v3.3.0 development line only when working on the separate 8-channel controller.

## Initial Setup

1. Install Arduino IDE.
2. Install ESP32 board support.
3. Connect the ESP32 controller to the computer.
4. Open the firmware project under `firmware/v3.2.9/` for the current stable 4-channel release.
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

## Validation

v3.2.9 was physically validated on the documented 4-channel hardware and passed the project CI firmware validation. The validated areas include:

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
- Light and dark themes

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

# Getting Started

This guide will help you set up the ESP32 Smart Relay Controller and prepare the device for its first use.

## Project Goal

The ESP32 Smart Relay Controller is designed as a reusable Wi-Fi relay-control platform for applications such as:

- Aquarium automation
- Terrarium automation
- Greenhouse and plant automation
- Irrigation and pump control
- Home automation
- General relay-controlled devices

## Current Baseline

The current stable firmware baseline is **v3.2.8**, based on the validated 4-channel relay hardware configuration. Aquarium-specific behavior can be adapted through configuration and application profiles so that the platform can be reused for other applications.

## Initial Setup

1. Install Arduino IDE.
2. Install ESP32 board support.
3. Connect the ESP32 controller to the computer.
4. Open the firmware project under `firmware/v3.2.8/`.
5. Select the appropriate ESP32 board and serial port.
6. Configure the device settings locally; do not commit personal Wi-Fi credentials.
7. Upload the firmware using USB for the initial installation.
8. Connect to the controller web interface.
9. After the initial installation is working, OTA can be used for supported firmware updates.

## Validated Relay Mapping

| Relay | GPIO |
|---:|---:|
| 1 | GPIO 5 |
| 2 | GPIO 17 |
| 3 | GPIO 16 |
| 4 | GPIO 4 |

The v3.2.8 relay board is active-LOW.

## Safety

This project can control mains-powered equipment. Use an appropriate enclosure, wiring, protection, isolation, and correctly rated components. Do not work on energized mains wiring.

## Related Documentation

- [Hardware](hardware.md)
- [Wiring](wiring.md)
- [Configuration](configuration.md)
- [Scheduler](scheduler.md)
- [OTA Updates](ota-update.md)
- [Troubleshooting](troubleshooting.md)

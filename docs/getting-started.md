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

The first firmware baseline will be based on the tested Aquarium Controller v2.0.1 implementation. Aquarium-specific behavior will gradually be moved into configuration or application profiles so that the same firmware can be reused for other applications.

## Initial Setup

1. Install Arduino IDE.
2. Install ESP32 board support.
3. Connect the ESP32 controller to the computer.
4. Open the firmware project under `firmware/`.
5. Select the appropriate ESP32 board and serial port.
6. Configure the device settings.
7. Upload the firmware.
8. Connect to the controller web interface.

## Safety

This project can control mains-powered equipment. Use an appropriate enclosure, wiring, protection, isolation, and correctly rated components. Do not work on energized mains wiring.

## Related Documentation

- [Hardware](hardware.md)
- [Wiring](wiring.md)
- [Configuration](configuration.md)
- [Scheduler](scheduler.md)
- [Troubleshooting](troubleshooting.md)

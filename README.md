# ESP32 Smart Relay Controller

A configurable Wi-Fi enabled ESP32 relay controller designed for aquariums, terrariums, greenhouses, irrigation systems, home automation, and other relay-based applications.

## Features

- ESP32-based relay control
- Web-based dashboard
- AUTO and MANUAL operating modes
- Multiple schedules
- Weekday scheduling
- Manual override
- Emergency OFF
- Configurable relay names and icons
- Wi-Fi configuration
- Runtime tracking
- Power and energy estimation
- Activity logging
- NTP time synchronization
- Persistent device configuration
- OTA update support planned

## Supported Applications

The same controller firmware can be configured for different applications:

- Aquarium automation
- Terrarium automation
- Greenhouse automation
- Irrigation systems
- Home automation
- Lighting control
- Pump and fan control
- General-purpose relay automation

## Project Architecture

The project separates generic controller functionality from application-specific configuration.

```text
ESP32 Smart Relay Controller
│
├── Wi-Fi / Network
├── Web Server / Web UI
├── Relay Manager
├── Scheduler
├── Settings Manager
├── Power Manager
├── Activity Log
└── Application Profile
    ├── Aquarium
    ├── Terrarium
    ├── Greenhouse
    └── Generic
```

## Hardware

The initial reference hardware is an ESP32 development board with a 4-channel relay board. Additional ESP32 boards and relay configurations can be documented as the project evolves.

> **Safety:** This project may be used to control mains-powered equipment. Use suitable electrical isolation, enclosure, protection, wiring, fusing, and components rated for the intended load. Never work on energized mains wiring.

## Repository Structure

```text
firmware/       Firmware releases and source
profiles/       Application-specific configurations
examples/       Example deployments
docs/           User and developer documentation
hardware/       Hardware and wiring documentation
images/         Project screenshots and diagrams
```

## Getting Started

1. Read `docs/getting-started.md`.
2. Review the hardware and wiring documentation.
3. Install Arduino IDE and ESP32 board support.
4. Open the firmware project.
5. Configure the controller.
6. Upload the firmware to the ESP32.
7. Connect to the device web interface.

## Aquarium Example

The first reference application is the aquarium controller. A typical configuration can be:

| Relay | Example Device | Example Power |
|---:|---|---:|
| 1 | Main Aquarium Light | 70 W |
| 2 | Air Pump | 15 W |
| 3 | Water Filter | 40 W |
| 4 | Heater | 100 W |

These values are examples only; configure the actual connected equipment and verify hardware ratings before use.

## Roadmap

- [x] Initial ESP32 relay controller
- [x] Web dashboard foundation
- [x] AUTO / MANUAL control
- [x] Scheduling foundation
- [x] Aquarium reference application
- [ ] Generic application profiles
- [ ] Refactor firmware into reusable modules
- [ ] OTA firmware update
- [ ] REST API
- [ ] MQTT support
- [ ] Home Assistant integration
- [ ] Sensor support
- [ ] Sensor-driven automation
- [ ] Additional relay hardware support

## Documentation

- [Getting Started](docs/getting-started.md)
- [Hardware](docs/hardware.md)
- [Wiring](docs/wiring.md)
- [Configuration](docs/configuration.md)
- [Scheduler](docs/scheduler.md)
- [Web UI](docs/web-ui.md)
- [Power Management](docs/power-management.md)
- [Troubleshooting](docs/troubleshooting.md)
- [OTA Updates](docs/ota-update.md)

## License

This project is released under the MIT License. See [LICENSE](LICENSE).

## Author

Vinay R M

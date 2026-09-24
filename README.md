# ESP32 Smart Relay Controller

A reusable Wi-Fi-enabled ESP32 relay controller designed for aquariums, terrariums, greenhouses, irrigation systems, lighting, pumps, fans, home automation, and other relay-based applications.

## v1.0.0 — Initial Stable Baseline

This is the first release of the **ESP32 Smart Relay Controller** repository.

The v1.0.0 firmware is based on the tested controller implementation validated on a 4-channel ESP32 relay board and successfully uploaded and updated over OTA. The aquarium setup is the first reference application used to validate the controller.

### Included capabilities

- 4-channel relay control
- AUTO / MANUAL modes
- Up to 6 schedules per relay
- Weekday scheduling
- Overnight schedules
- 24×7 schedules
- Manual override
- Emergency OFF and ALL OFF
- Resume AUTO / Resume ALL
- Configurable relay names and icons
- Wi-Fi configuration through the web interface
- NTP / IST time synchronization
- Runtime tracking
- Persistent activity logging
- Per-relay power settings
- Estimated energy usage
- Browser backup and restore
- Responsive web dashboard
- Light / dark theme
- Arduino OTA updates
- mDNS support
- Startup-safe relay initialization

## First Reference Application

The initial validation application is aquarium automation. The same controller architecture is intended to be reusable for other relay-controlled applications without tying the repository to a single device type.

## Repository Direction

The project will progressively separate reusable controller functionality from application-specific profiles:

```text
ESP32 Smart Relay Controller
│
├── Core controller
│   ├── Relay manager
│   ├── Scheduler
│   ├── Wi-Fi / network
│   ├── Web UI
│   ├── Runtime tracking
│   ├── Power estimation
│   └── Activity log
│
├── Profiles
│   ├── Aquarium
│   ├── Terrarium
│   ├── Greenhouse
│   └── Generic
│
└── Hardware
    ├── ESP32 boards
    └── Relay boards
```

## Hardware

The initial reference hardware is an ESP32 development board with a 4-channel relay board.

The current validated relay mapping is:

| Relay | ESP32 GPIO |
|---:|---:|
| 1 | GPIO 5 |
| 2 | GPIO 17 |
| 3 | GPIO 16 |
| 4 | GPIO 4 |

The relay outputs use active-LOW logic in the current baseline firmware.

> **Safety:** This project may control mains-powered equipment. Use suitable isolation, enclosure, protection, wiring, fusing, and components rated for the intended load. Never work on energized mains wiring.

## Wi-Fi and Credentials

Wi-Fi credentials are **not embedded in the repository source code**. Configure the controller through its Wi-Fi configuration interface. Do not commit personal Wi-Fi credentials, API keys, tokens, certificates, or other secrets.

## Getting Started

1. Install Arduino IDE and ESP32 board support.
2. Open `firmware/v1.0.0/ESP32_Smart_Relay_Controller.ino`.
3. Select the appropriate ESP32 board.
4. Configure the controller through its Wi-Fi setup interface.
5. Upload the firmware by USB for initial installation, or use OTA when the device is already configured and reachable on the network.
6. Open the controller web interface and configure relays and schedules.

## Versioning

The project follows Semantic Versioning:

- **Patch** (`v1.0.x`) — bug fixes and small corrections.
- **Minor** (`v1.x.0`) — backward-compatible features.
- **Major** (`v2.0.0`) — breaking architecture or behavior changes.

## Roadmap

- [x] Initial stable controller baseline
- [x] Aquarium reference validation
- [x] OTA update support
- [ ] Generic application profiles
- [ ] Modular controller core
- [ ] ESP32 firmware CI build validation
- [ ] Additional relay hardware support
- [ ] Sensor support
- [ ] REST API
- [ ] MQTT support
- [ ] Home Assistant integration

## License

This project is released under the MIT License. See [LICENSE](LICENSE).

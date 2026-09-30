# ESP32 Smart Relay Controller

A reusable Wi-Fi-enabled ESP32 relay controller designed for aquariums, terrariums, greenhouses, irrigation systems, lighting, pumps, fans, home automation, and other relay-based applications.

## v1.0.0 — Initial Stable Baseline

This is the first stable release of the **ESP32 Smart Relay Controller** repository.

The v1.0.0 firmware is based on the tested controller implementation validated on a 4-channel ESP32 relay board and successfully uploaded and updated over OTA. The aquarium setup is the first reference application used to validate the controller.

## v2.0.1 — Current Software Baseline

The v2.0.1 firmware extends the tested controller architecture with a modern responsive dashboard, runtime monitoring, power estimation, scheduling controls, persistent settings, and improved monitoring information.

The v2.0.1 software baseline has been validated by the repository CI build. **Hardware validation of the v2.0.1 changes is pending.** The previously hardware-tested v1.0.0 implementation remains the hardware reference baseline until the ESP32 hardware is available for testing.

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
- Estimated electricity cost
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

The current firmware is intentionally configured for four relay channels:

| Relay | ESP32 GPIO |
|---:|---:|
| 1 | GPIO 5 |
| 2 | GPIO 17 |
| 3 | GPIO 16 |
| 4 | GPIO 4 |

The relay outputs use active-LOW logic in the current baseline firmware.

An 8-channel ESP32 relay board may be used later as a physical test platform while keeping the firmware configured for four channels. Additional relay channels are not considered validated until their GPIO mapping and electrical interface are explicitly verified.

> **Safety:** This project may control mains-powered equipment. Use suitable isolation, enclosure, protection, wiring, fusing, and components rated for the intended load. Never work on energized mains wiring.

## Wi-Fi and Credentials

Wi-Fi credentials are **not embedded in the repository source code**. Configure the controller through its Wi-Fi configuration interface. Do not commit personal Wi-Fi credentials, API keys, tokens, certificates, or other secrets.

## Getting Started

1. Install Arduino IDE and ESP32 board support.
2. For the previously hardware-tested reference, open `firmware/v1.0.0/ESP32_Smart_Relay_Controller.ino`.
3. For the current software baseline, open `firmware/v2.0.1/ESP32_Smart_Relay_Controller.ino`.
4. Select the appropriate ESP32 board.
5. Configure the controller through its Wi-Fi setup interface.
6. Upload the firmware by USB for initial installation, or use OTA when the device is already configured and reachable on the network.
7. Open the controller web interface and configure relays, schedules, and power settings.

## CI / Build Validation

GitHub Actions builds the current `firmware/v2.0.1/ESP32_Smart_Relay_Controller.ino` source for the `esp32:esp32:esp32` target on pull requests and pushes to `main`.

CI validation confirms that the firmware compiles successfully. It does **not** replace physical ESP32 hardware validation.

## Versioning

The project follows Semantic Versioning:

- **Patch** (`v1.0.x`) — bug fixes and small corrections.
- **Minor** (`v1.x.0`) — backward-compatible features.
- **Major** (`v2.0.0`) — breaking architecture or behavior changes.

## Roadmap

- [x] Initial stable controller baseline
- [x] Aquarium reference validation
- [x] OTA update support
- [x] ESP32 firmware CI build validation
- [x] Runtime and power monitoring
- [x] Responsive monitoring dashboard
- [ ] Hardware validation of v2.0.1
- [ ] Generic application profiles
- [ ] Modular controller core
- [ ] Additional relay hardware support
- [ ] Sensor support
- [ ] REST API
- [ ] MQTT support
- [ ] Home Assistant integration

## License

This project is released under the MIT License. See [LICENSE](LICENSE).

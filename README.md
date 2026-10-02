# ESP32 Smart Relay Controller

A Wi-Fi-enabled ESP32 relay controller with a responsive web dashboard, scheduling, runtime/power estimation, persistent activity logging, emergency control, and OTA updates.

## Current stable release — v3.2.8

**v3.2.8 is the validated 4-relay aquarium-controller baseline.** It has been physically tested on the current ESP32 + 4-channel relay hardware.

### Validated features

- 4-channel relay control
- Fast manual ON/OFF response
- AUTO / MANUAL modes
- Up to 6 schedules per relay
- Weekday scheduling
- Overnight schedules
- Automatic schedule ON/OFF execution
- Next scheduled action display
- Emergency ALL OFF with previous-state resume
- Per-relay emergency OFF / resume
- NTP / IST time synchronization
- Timestamped activity log
- Persistent settings and logs
- Runtime tracking
- Estimated power and energy usage
- Estimated electricity cost
- Configurable relay names and icons
- Wi-Fi configuration through the web interface
- Responsive desktop/mobile dashboard
- Light/dark theme
- Browser backup/restore
- Arduino OTA updates
- mDNS support
- Startup-safe relay initialization

## Hardware baseline

The validated hardware configuration uses an ESP32 development board and a 4-channel active-LOW relay board.

| Relay | ESP32 GPIO |
|---:|---:|
| 1 | GPIO 19 |
| 2 | GPIO 18 |
| 3 | GPIO 5 |
| 4 | GPIO 17 |

**Do not assume other GPIO mappings are validated.** The 8-channel test work from earlier development is not part of the stable release.

> **Safety:** The ESP32 GPIOs control the low-voltage relay inputs. Any mains-voltage wiring must use suitable isolation, enclosure, protection, fusing, wire sizing, and components rated for the load, and should be installed by a qualified person. Software wattage values are estimates and are not electrical safety ratings.

## Firmware layout

```text
firmware/
├── v1.0.0/   # historical stable baseline
├── v2.0.1/   # historical development baseline
└── v3.2.8/   # current validated release
```

Historical firmware is retained for traceability. New development should start from v3.2.8 rather than modifying an older baseline.

## Getting started

1. Install Arduino IDE and ESP32 board support.
2. Open the v3.2.8 firmware from `firmware/v3.2.8/`.
3. Select the appropriate ESP32 board.
4. Configure Wi-Fi through the controller setup/configuration interface.
5. Upload by USB for the initial installation.
6. Open the controller web interface using the ESP32 IP address or mDNS hostname when available.
7. Configure relay names, wattages, schedules, weekdays, and modes.
8. Use OTA for subsequent firmware updates when the ESP32 is reachable on the network.

### Credentials

**Never commit personal Wi-Fi credentials, API keys, tokens, certificates, or other secrets.** The repository intentionally contains placeholder/default values only. Configure real credentials on the device.

## Scheduler behavior

Schedules are configured independently per relay. Each schedule contains:

- Start time
- Stop time
- Enabled state
- Selected weekdays

AUTO mode applies the configured schedules. Manual control remains available according to the controller's manual-override rules. Overnight schedules are supported.

## Emergency behavior

**Emergency ALL OFF** immediately turns all relays off while preserving their pre-emergency states. **Resume ALL** restores the states that were active immediately before the emergency action.

## Power and runtime

Power and energy values are software estimates based on the wattage configured for each relay and the measured relay runtime. They are **not measurements from an electrical power meter**.

## OTA

The controller exposes Arduino OTA support after network initialization. Keep the ESP32 and development computer on the same reachable network and use the configured controller hostname/device entry for OTA updates.

## CI

GitHub Actions validates that the current v3.2.8 firmware compiles for the ESP32 target. CI compilation does not replace physical hardware validation.

## Repository structure

```text
ESP32-Smart-Relay-Controller/
├── firmware/       # versioned firmware
├── docs/            # user and developer documentation
├── hardware/       # board and wiring references
├── profiles/        # reusable application configuration examples
├── examples/        # application examples
└── .github/         # repository automation
```

## Development policy

The v3.2.8 firmware is the known-good baseline. Bug fixes should preserve the validated relay-control, scheduler, emergency-resume, timing, and UI behavior. New features should be developed as a new version and validated before replacing the stable baseline.

## License

This project is released under the MIT License. See [LICENSE](LICENSE).

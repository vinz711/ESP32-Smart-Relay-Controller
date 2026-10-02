# Firmware v3.2.8

Stable 4-relay aquarium-controller baseline.

## Validated hardware

| Relay | GPIO |
|---:|---:|
| 1 | 19 |
| 2 | 18 |
| 3 | 5 |
| 4 | 17 |

Relay board logic is active-LOW.

## Validated functionality

- Fast manual relay control
- AUTO/MANUAL modes
- Up to 6 schedules per relay
- Weekday and overnight schedules
- Automatic schedule ON/OFF
- Next scheduled action
- Emergency ALL OFF with previous-state restoration
- NTP/IST time synchronization
- Timestamped activity logging
- Runtime and estimated power/energy tracking
- Persistent settings/logs
- Wi-Fi configuration
- Responsive dashboard
- Arduino OTA
- Browser backup/restore

## Security

Do not add personal Wi-Fi credentials or other secrets to the firmware source. Configure device credentials locally.

## Release status

This version is the known-good hardware-tested baseline. Future feature work should branch from this version and use a new semantic version.

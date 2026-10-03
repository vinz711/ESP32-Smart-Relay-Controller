# Project Roadmap

This roadmap describes the planned direction of the ESP32 Smart Relay Controller.

## Completed — v3.2.8

The v3.2.8 baseline has been implemented and hardware tested with the 4-channel relay configuration.

- Responsive web dashboard with relay controls
- AUTO and MANUAL relay modes
- Multiple schedules per relay
- Start/stop times and weekday selection
- Overnight schedules
- Scheduler execution on the ESP32
- Next scheduled action display
- Activity logging with synchronized local date/time
- Emergency ALL OFF with restoration of the pre-emergency relay state
- Runtime tracking and estimated power/energy usage
- Per-relay power settings and cost estimation
- Wi-Fi configuration and web-based control
- OTA firmware updates
- mDNS support
- Safe relay initialization and startup behavior
- Final 4-channel GPIO mapping:
  - Relay 1: GPIO 5
  - Relay 2: GPIO 17
  - Relay 3: GPIO 16
  - Relay 4: GPIO 4
- CI firmware validation
- Final hardware validation

## In Progress — v3.2.9

v3.2.9 is the updated 4-channel Smart Management development/hardware-validation baseline.

Current scope includes:

- Smart Management desktop dashboard
- Responsive mobile dashboard
- Light and dark themes
- Updated relay control cards
- Temporary manual control
- Feeding / Maintenance schedule pause
- Emergency OFF / Resume improvements
- Updated power settings presentation
- Improved system status presentation
- Updated persistent activity-log presentation
- Updated project screenshots and UI documentation

v3.2.9 must complete firmware, CI, and physical hardware validation before it is promoted to the stable release baseline.

## Separate Development Line — v3.3.0

The 8-channel relay controller is being developed separately from the 4-channel v3.2.9 line.

The 8-channel work uses a different GPIO mapping and requires its own CI and physical hardware-validation cycle before release.

## Future Direction

Longer-term development may include:

- A cleaner hardware abstraction/configuration layer for different ESP32 boards and relay configurations
- Additional application and hardware profiles
- Optional external power-measurement integration
- Additional automation and integration options
- Further reliability and maintainability improvements

Future features should preserve the stable behavior established by the validated v3.2.8 baseline.

## Development Policy

- Do not modify the published `v3.2.8` tag.
- Start new work from `main` in a feature branch such as `feature/v3.2.9-<feature>`.
- Use pull requests for changes to `main`.
- Require CI validation before merging.
- Perform hardware validation for firmware or hardware-related changes.
- Create a new versioned release after the relevant changes have been validated.

# Project Roadmap

This roadmap describes the planned direction of the ESP32 Smart Relay Controller. The stable v3.2.8 release is the current validated baseline.

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

## Next — v3.2.9+

Future development should begin from `main` using a dedicated feature branch and pull request. Changes should be CI-tested and hardware-tested before becoming part of a release.

Potential areas for the next development cycle include:

- Further dashboard and UI/UX refinement
- Additional diagnostics and system-status information
- Improved monitoring and troubleshooting support
- Expanded hardware-specific configuration profiles
- Additional ESP32 and relay-board compatibility
- Documentation and installation improvements

These items are candidates for future development and are not commitments to a particular release date or implementation order.

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

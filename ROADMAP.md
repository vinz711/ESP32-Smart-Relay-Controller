# Project Roadmap

This roadmap describes the planned direction of the ESP32 Smart Relay Controller.

## Completed — v3.2.9

v3.2.9 is the current stable 4-channel Smart Management release. It has been implemented, CI validated, physically tested on the documented 4-channel relay hardware, and published as the stable release.

- Smart Management desktop dashboard
- Responsive mobile dashboard
- Light and dark themes
- Configurable relay names and icons
- AUTO and MANUAL relay modes
- Multiple schedules per relay
- Start/stop times and weekday selection
- Overnight schedules
- Temporary manual control
- Feeding / Maintenance schedule pause
- Emergency OFF / Resume and Emergency ALL OFF / Resume ALL
- Next scheduled action display
- Activity logging with synchronized local date/time
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
- Physical hardware validation
- v3.2.9 release/tag publication

## Current Development Line — v3.3.0

The 8-channel relay controller is being developed separately from the stable 4-channel v3.2.9 line.

The 8-channel work uses a different GPIO mapping and requires its own CI and physical hardware-validation cycle before release.

## Future Direction

Longer-term development may include:

- A cleaner hardware abstraction/configuration layer for different ESP32 boards and relay configurations
- Additional application and hardware profiles
- Optional external power-measurement integration
- Additional automation and integration options
- Further reliability and maintainability improvements

Future features should preserve the stable behavior established by v3.2.9.

## Development Policy

- Do not modify published release tags.
- Start new work from the current `main` state in a dedicated feature or development branch.
- Keep the v3.3.0 8-channel line separate from the stable 4-channel release.
- Use pull requests for changes to `main`.
- Require CI validation before merging.
- Perform hardware validation for firmware or hardware-related changes.
- Create a new versioned release only after the relevant changes have been validated.

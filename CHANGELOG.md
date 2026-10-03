# Changelog

## [v3.2.9] - 2026-10-03

### Added

- Updated Smart Management desktop dashboard
- Responsive mobile dashboard
- Light and dark themes
- Configurable relay names and icons
- Temporary manual control
- Individual Emergency OFF / Resume
- Emergency ALL OFF / Resume ALL
- Feeding / Maintenance pause
- Per-relay power configuration
- Estimated power and energy display
- Browser backup and restore
- Updated system status display
- Updated persistent activity log interface

### Improved

- Relay control presentation
- Schedule visibility
- Next scheduled action display
- Runtime presentation
- Power and energy presentation
- Mobile usability
- Desktop dashboard layout
- Activity log usability

### Hardware

- Maintains the validated 4-channel GPIO configuration:
  - Relay 1 → GPIO 5
  - Relay 2 → GPIO 17
  - Relay 3 → GPIO 16
  - Relay 4 → GPIO 4

### Validation

v3.2.9 is the updated 4-channel Smart Management firmware baseline.

Physical hardware, firmware, CI, OTA, scheduling, emergency control, and responsive UI validation should be completed before promoting this version to the stable release baseline.

### Separation from v3.3.0

The 8-channel relay development remains on the separate v3.3.0 development line and is not part of v3.2.9.

# Changelog

All notable changes to this project are documented here.

## v3.2.9 — 4-Relay Smart Management UI

- Updated the 4-channel controller to the Smart Management dashboard design.
- Preserved the validated GPIO mapping: R1=5, R2=17, R3=16, R4=4.
- Added responsive desktop and mobile UI layouts.
- Added light/dark theme support.
- Added summary cards for active relays, runtime, estimated energy, and next scheduled start.
- Added improved relay cards with status, mode, schedule, next action, temporary control, settings, and emergency controls.
- Added temporary manual control durations in AUTO mode.
- Added Feeding / Maintenance schedule pause for selected AUTO relays.
- Added per-relay power rating editing with software-based energy estimation.
- Improved system status and network information display.
- Improved searchable/exportable persistent activity log presentation.
- Preserved device name/icon configuration, backup/restore, Wi-Fi configuration, and OTA support.
- Preserved emergency OFF state capture and Resume behavior.
- Preserved startup-safe relay initialization and active-LOW relay logic.
- Sanitized firmware defaults so personal Wi-Fi credentials are not committed.

**Validation status:** development/hardware-validation baseline. v3.2.8 remains the validated stable 4-channel release until the v3.2.9 firmware, CI, and physical hardware validation cycle is complete.

## v3.2.8 — Stable 4-Relay Aquarium Controller

- Promoted the validated 4-relay aquarium controller to the stable baseline.
- GPIO mapping: R1=5, R2=17, R3=16, R4=4.
- Preserved fast manual relay response.
- Fixed automatic schedule execution and schedule boundary handling.
- Added reliable IST time handling for scheduling and activity logs.
- Added upcoming/next scheduled action calculation.
- Fixed emergency ALL OFF so the exact pre-emergency relay states can be restored by Resume ALL.
- Preserved AUTO/MANUAL operation, multiple schedules, weekday scheduling and overnight schedules.
- Preserved runtime and estimated power/energy tracking.
- Preserved persistent settings, activity logs, Wi-Fi configuration, backup/restore and OTA.
- Sanitized repository defaults so personal Wi-Fi credentials are not committed.

## v2.0.1 — Historical Software Baseline

- Added responsive dashboard, runtime monitoring, power estimation, scheduling controls, persistent settings and monitoring information.

## v1.0.0 — Initial Stable Baseline

- Established the ESP32 Smart Relay Controller as a standalone generic project.
- Added the first stable 4-channel controller firmware baseline.
- Preserved the tested aquarium controller behavior as the initial validation application.
- Included AUTO / MANUAL operation and multiple schedules.
- Included weekday, overnight and 24×7 scheduling.
- Included manual override and emergency OFF controls.
- Included configurable relay names/icons and power settings.
- Included runtime tracking, activity logging and energy estimation.
- Included Wi-Fi configuration, NTP / IST, mDNS and Arduino OTA support.

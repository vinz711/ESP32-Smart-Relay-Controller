# Changelog

All notable changes to this project are documented here.

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

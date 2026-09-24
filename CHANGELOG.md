# Changelog

All notable changes to this project will be documented here.

## [Unreleased]

- Establish generic ESP32 Smart Relay Controller repository structure.
- Prepare application profiles for aquarium, terrarium, greenhouse, and generic use.
- Preserve the tested aquarium controller as the initial firmware baseline.

## v2.0.1

The initial reference firmware release is based on the tested aquarium controller implementation.

### Features

- ESP32 Wi-Fi relay control
- Web-based control dashboard
- AUTO / MANUAL modes
- Multiple schedules
- Weekday scheduling
- Manual relay control
- Emergency OFF behavior
- Configurable relay names
- Wi-Fi configuration through the web interface
- Activity logging
- Runtime tracking
- Power usage estimation

> Historical development files are retained in the original private `Scripts` repository. The new repository will contain the cleaned and reusable project baseline.

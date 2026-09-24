# Configuration

The controller is intended to use configuration rather than application-specific firmware.

## Relay Configuration

Each relay can have configurable properties such as:

- Relay number
- Display name
- Icon/type
- Operating mode
- Scheduled periods
- Rated/estimated power in watts

Example applications can therefore use the same firmware:

- Aquarium: Light, Air Pump, Filter, Heater
- Terrarium: Light, Fan, Mist Pump, Heater
- Greenhouse: Grow Light, Water Pump, Fan, Valve

## Secrets

Do not store Wi-Fi passwords, API tokens, or other credentials in the repository. Use the device configuration mechanism or a local secrets file that is excluded by `.gitignore`.

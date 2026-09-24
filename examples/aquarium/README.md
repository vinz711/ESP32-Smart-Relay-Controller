# Aquarium Example

This is the first reference application for the ESP32 Smart Relay Controller.

Example relay assignments:

| Relay | Device | Typical purpose |
|---:|---|---|
| 1 | Main Aquarium Light | Scheduled lighting |
| 2 | Air Pump | Continuous or scheduled aeration |
| 3 | Water Filter | Filtration/pump control |
| 4 | Heater | Temperature equipment |

The actual device names, schedules, and wattage values should be configured for the individual aquarium.

See `profiles/aquarium/aquarium_config.json` for the example profile.

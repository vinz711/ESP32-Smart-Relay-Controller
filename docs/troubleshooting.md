# Troubleshooting

## Controller does not connect to Wi-Fi

- Verify the configured SSID and password.
- Confirm the ESP32 is within Wi-Fi range.
- Check serial output during startup.
- Verify that the configured network is supported by the ESP32 hardware.

## Relay does not switch

- Check controller power.
- Check relay-board power.
- Verify the GPIO mapping for the exact hardware revision.
- Check the relay state in the web UI and serial log.

## Schedule does not run

- Verify that the controller has valid time information.
- Check the selected weekdays.
- Check the schedule start/stop times.
- Confirm the relay is in AUTO mode.
- Check whether Emergency OFF is active.

For mains-related problems, disconnect power and have the wiring inspected by a suitably qualified person.

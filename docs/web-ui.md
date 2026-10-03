# Web UI

The controller provides a responsive Smart Management web dashboard for relay control and configuration.

## v3.2.9 UI

The v3.2.9 4-channel dashboard is organized into the following areas:

- Summary cards for active relays, today's runtime, estimated energy usage, and next scheduled start
- Relay Control cards for all four channels
- Relay state and AUTO / MANUAL mode indicators
- Configurable relay names and icons
- Schedule summary and next-action information
- Temporary manual control duration
- Per-relay ON/OFF controls
- Per-relay Schedule and Settings controls
- Per-relay Emergency OFF / Resume AUTO
- Power Settings with per-device wattage configuration
- Software-based energy and cost estimation
- Quick Settings for names/icons, default schedules, Feeding / Maintenance, Wi-Fi, OTA, backup/restore, and factory reset
- System Status with ESP32 connection, IP address, RSSI, uptime, and network/time information
- Persistent searchable/exportable Activity Log
- Light and dark themes
- Responsive desktop and mobile layouts

## Responsive behavior

Desktop uses a two-column relay-control layout and a four-column power-settings layout. On smaller screens the relay cards and management panels collapse into a single-column mobile layout.

## Application-neutral design

The UI remains application-neutral so the same dashboard can control aquarium equipment, pumps, lights, fans, valves, or other relay devices. Default aquarium names/icons are only initial values and can be changed from the controller.

## Power information

Power and energy values shown by the dashboard are software estimates based on configured relay wattage and runtime. The controller does not contain an electrical power sensor.

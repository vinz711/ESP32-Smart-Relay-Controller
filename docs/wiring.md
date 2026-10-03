# Wiring

This page documents the validated low-voltage ESP32-to-relay control mapping. It intentionally does not provide a mains wiring diagram because load-side wiring must follow the actual relay-board schematic, ratings, enclosure, protection requirements, and local electrical rules.

## ESP32 to 4-Channel Relay Board

| Relay channel | ESP32 GPIO | Logic |
|---|---:|---|
| Relay 1 | GPIO 5 | LOW = ON |
| Relay 2 | GPIO 17 | LOW = ON |
| Relay 3 | GPIO 16 | LOW = ON |
| Relay 4 | GPIO 4 | LOW = ON |

The relay outputs are initialized OFF during ESP32 startup before normal controller operation begins.

## Power

- Use the relay-board/controller supply recommended by the actual board manufacturer.
- Confirm the board's 5 V input and current requirements before connecting the supply.
- Do not power a relay-board load through an ESP32 GPIO pin.
- Keep low-voltage control wiring separated from hazardous-voltage wiring.

## Load-Side Connections

The relay board's `COM`, `NO`, and `NC` terminals are load-side connections. The correct terminal arrangement depends on whether the application requires normally-open or normally-closed behavior.

Do not assume terminal polarity or contact arrangement from the software GPIO mapping. Verify the markings printed on the actual relay board and its schematic.

## Protection and Enclosure

Use appropriate:

- Fuse or circuit protection
- Wire gauge
- Connectors and terminals
- Enclosure
- Strain relief
- Isolation/clearance
- Relay contact ratings

> **Safety:** Do not connect mains voltage based only on this documentation. Verify the actual hardware documentation and local electrical requirements. Mains wiring should be installed and tested by a qualified person.

# Relay Board

## Validated v3.2.8 Hardware

The current stable v3.2.8 hardware baseline uses an ESP32-compatible 4-channel, active-LOW relay board.

### Validated GPIO Mapping

| Relay | GPIO |
|---:|---:|
| 1 | GPIO 5 |
| 2 | GPIO 17 |
| 3 | GPIO 16 |
| 4 | GPIO 4 |

This mapping has been validated with the v3.2.8 firmware and hardware test configuration.

### Electrical and Power Considerations

The exact relay contact ratings and power requirements depend on the physical relay board used. Verify the board documentation and ratings before connecting a load. Ensure the ESP32 and relay board are powered appropriately and that the control interface is compatible.

## Other Hardware Variants

Other 4-channel or multi-channel relay boards may differ in GPIO mapping, active-high/active-low behavior, power requirements, isolation, or contact ratings. Do not assume the v3.2.8 configuration applies to another board. A new hardware variant should have its configuration documented and tested separately before being treated as a supported profile.

## Safety

Relay contact ratings alone do not guarantee that an assembled relay board is suitable for every load. Check the complete board design, terminal ratings, isolation, wiring, protection, enclosure, and the characteristics of the connected load.

Do not connect mains voltage directly to ESP32 GPIOs. Disconnect power before changing wiring and have mains-voltage work performed by a suitably qualified person where required.

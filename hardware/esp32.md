# ESP32 Hardware

The stable v3.2.8 baseline targets an ESP32 development board connected to a 4-channel active-LOW relay board.

## Validated GPIO map

| Relay | GPIO |
|---:|---:|
| 1 | 5 |
| 2 | 17 |
| 3 | 16 |
| 4 | 4 |

The mapping above is the validated configuration for the current v3.2.8 hardware baseline.

## Board setup

- Arduino IDE with ESP32 board support
- Select the appropriate ESP32 development-board target
- Serial connection is used for initial USB flashing and diagnostics
- USB provides the normal development/programming connection
- Relay inputs are low-voltage GPIO control signals

## Important

Do not connect mains voltage directly to ESP32 GPIOs. Use an appropriately rated relay module, enclosure, isolation and protection. Verify the relay board's electrical interface before connecting loads.

Other ESP32 or relay-board variants may require a different GPIO mapping and must be documented and validated separately.

# Power Management

The controller can use configured relay wattage values to estimate runtime-based energy usage.

For a simple estimate:

`Energy (kWh) = Power (W) × Runtime (hours) / 1000`

These values are estimates and should not be treated as calibrated electrical measurements unless the hardware includes suitable measurement circuitry.

## Example

A 40 W load running for 5 hours:

`40 × 5 / 1000 = 0.20 kWh`

Actual power consumption depends on the connected equipment and its operating characteristics.

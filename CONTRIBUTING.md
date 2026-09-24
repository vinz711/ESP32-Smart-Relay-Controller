# Contributing

Thanks for your interest in the ESP32 Smart Relay Controller.

## Development Principles

- Keep the core controller generic and application-independent.
- Prefer configuration and profiles over hard-coded application behavior.
- Do not commit Wi-Fi passwords, API keys, tokens, or other secrets.
- Preserve tested behavior when refactoring the firmware.
- Document hardware-specific assumptions and safety considerations.

## Suggested Workflow

1. Create a feature branch.
2. Make a focused change.
3. Test the firmware on supported hardware.
4. Update documentation when behavior changes.
5. Open a pull request with a clear description and test information.

## Hardware Safety

Changes involving mains-powered relay hardware must be reviewed carefully. Never test energized mains wiring without appropriate electrical knowledge, isolation, protection, and equipment.

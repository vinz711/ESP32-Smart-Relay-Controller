# OTA Updates

The v3.2.9 firmware includes Arduino OTA support. OTA can be used for subsequent firmware updates after the device has been installed and is reachable on the local network.

## Initial installation

Use the supported USB flashing procedure for the first installation or for recovery when OTA is unavailable.

## OTA workflow

1. Flash the intended firmware to the ESP32 using USB.
2. Connect the ESP32 to the configured Wi-Fi network.
3. Confirm the controller is reachable on the local network.
4. Build the intended firmware version with Arduino OTA support enabled.
5. Start the OTA upload from the development environment.
6. Wait for the upload and device restart to complete.
7. Verify the web dashboard and relay operation after reboot.

## OTA authentication

The public v3.2.9 source does **not** contain a personal or fixed OTA password. Keep OTA on a trusted local network and do not expose the OTA service directly to the public internet.

If OTA authentication is added in a future release, the credential should be provisioned securely and must not be committed to the public repository.

## Safety and recovery

- Do not interrupt power during a firmware update.
- Perform OTA updates only on a trusted local network.
- Do not expose the OTA service directly to the public internet.
- Keep a USB recovery path available.
- Hardware-test firmware changes after OTA when the change affects relay, scheduler, timing, networking, or hardware behavior.

## v3.2.9 status

OTA support is part of the v3.2.9 development baseline and must be included in the firmware/CI/hardware validation cycle before v3.2.9 is promoted to the stable release baseline.

## Stable baseline

Arduino OTA functionality was validated as part of the stable v3.2.8 baseline. Future firmware releases should preserve a recoverable USB installation path and validate OTA behavior before release.

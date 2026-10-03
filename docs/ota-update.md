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

The public v3.2.9 source does **not** contain a fixed or reusable OTA password. Do not add a shared password, Wi-Fi credential, token, or other secret directly to the firmware source.

OTA should be used only on a trusted local network. Do not expose the OTA service directly to the public internet.

Because the current public firmware does not configure OTA authentication, treat LAN access to the ESP32 as trusted administrative access and keep the controller behind the home/network firewall.

## Safety and recovery

- Do not interrupt power during a firmware update.
- Perform OTA updates only on a trusted local network.
- Do not expose the OTA service directly to the public internet.
- Keep a USB recovery path available.
- Hardware-test firmware changes after OTA when the change affects relay, scheduler, timing, networking, or hardware behavior.

## v3.2.9 status

OTA support is included in the stable v3.2.9 release. The release was physically validated on the documented 4-channel hardware. Future firmware changes must be validated before being promoted to a new stable release.

## Stable baseline

v3.2.9 is the current stable 4-channel baseline. The published `v3.2.9` tag is immutable. Keep the USB installation/recovery path available for all future OTA-enabled releases.

/*
  VINAY'S AQUARIUM CONTROLLER - v3.2.8
  ESP32 4-Channel Relay + Smart Management UI

  Tested baseline preserved from v2.0.1:
    Relay 1 -> GPIO 19
    Relay 2 -> GPIO 18
    Relay 3 -> GPIO 5
    Relay 4 -> GPIO 17
    Active LOW: LOW = ON, HIGH = OFF

  Features:
    - Wi-Fi control + configurable SSID/password
    - Persistent settings (Preferences)
    - AUTO / MANUAL mode
    - Up to 6 schedules per relay
    - Weekday scheduling
    - Overnight schedules
    - 24x7 schedules (00:00 -> 00:00 when enabled)
    - Manual override outside an active AUTO schedule
    - Emergency OFF per relay + Resume AUTO
    - Emergency ALL OFF + Resume ALL
    - ArduinoOTA firmware update
    - mDNS hostname: Aquarium-Controller.local
    - NTP / IST time
    - Runtime tracking
    - Persistent activity log (up to 300 events)
    - Custom relay names/icons
    - Power rating per relay
    - Estimated energy usage (kWh)
    - Browser backup/restore
    - Light/dark theme
    - Responsive mobile UI
*/

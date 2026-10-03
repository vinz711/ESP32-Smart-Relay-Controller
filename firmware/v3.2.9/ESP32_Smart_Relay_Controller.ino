/*
  AQUARIUM CONTROLLER - v3.2.9
  ESP32 4-Channel Relay + Smart Management UI

  Tested 4-channel baseline:
    Relay 1 -> GPIO 5
    Relay 2 -> GPIO 17
    Relay 3 -> GPIO 16
    Relay 4 -> GPIO 4
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
    - Startup-safe: all relays OFF before settings/Wi-Fi init

  IMPORTANT:
    This code controls the LOW-VOLTAGE GPIO inputs of the relay board.
    230V wiring must be installed inside a suitable enclosure by a
    qualified electrician. The software power rating is NOT a relay
    safety rating.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <time.h>
#include <sys/time.h>
#include <LittleFS.h>
#include <esp_system.h>

#define FW_VERSION "3.2.9"
#define RELAY_COUNT 4
#define MAX_SCHEDULES 6
#define MAX_LOGS 300

const char* DEVICE_HOSTNAME = "Aquarium-Controller";

// ---------------- WIFI DEFAULTS ----------------
const char* DEFAULT_WIFI_SSID = "";
const char* DEFAULT_WIFI_PASSWORD = "";

// Fallback setup AP used only when the saved Wi-Fi cannot be reached.
const char* FALLBACK_AP_SSID = "Aquarium-Controller-Setup";
const char* FALLBACK_AP_PASSWORD = "change-me";

// ---------------- RELAY PINS ----------------
// Validated 4-channel board mapping.
const uint8_t relayPins[RELAY_COUNT] = {5, 17, 16, 4};

// Active LOW relay board
const uint8_t RELAY_ON = LOW;
const uint8_t RELAY_OFF = HIGH;

// ---------------- DEFAULT DEVICE DATA ----------------
const char* DEFAULT_NAMES[RELAY_COUNT] = {
  "Main Tank Filter",
  "Aquarium Lights",
  "Wave Maker",
  "Heater"
};

/*
   ============================================================
   ESP32 SMART RELAY CONTROLLER
   ESP32 4-Channel Relay Controller
   ============================================================

   Relay GPIO:
   Relay 1 -> GPIO 5
   Relay 2 -> GPIO 17
   Relay 3 -> GPIO 16
   Relay 4 -> GPIO 4

   Relay logic:
   ACTIVE LOW

   Features:
   - Wi-Fi control
   - Wi-Fi SSID/password configurable from webpage
   - Persistent settings using Preferences
   - AUTO / MANUAL mode
   - Multiple schedules per relay
   - Emergency OFF override
   - ArduinoOTA firmware update
   - Weekday scheduling: Mon Tue Wed Thu Fri Sat Sun
   - Overnight schedules
   - Custom relay names
   - Custom relay icons
   - Runtime tracking
   - Activity log
   - Dark / Light theme
   - Desktop 2x2 layout
   - Mobile responsive layout
   - All OFF
   - NTP / IST time
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <ArduinoOTA.h>

// ============================================================
// DEFAULT WIFI
// ============================================================

const char* DEFAULT_WIFI_SSID = "";
const char* DEFAULT_WIFI_PASSWORD = "";

// ============================================================
// RELAY CONFIGURATION
// ============================================================

#define RELAY_COUNT 4

const uint8_t relayPins[RELAY_COUNT] = {
  5,
  17,
  16,
  4
};

// Active LOW relay
const uint8_t RELAY_ON  = LOW;
const uint8_t RELAY_OFF = HIGH;

// ============================================================
// WEB SERVER
// ============================================================

WebServer server(80);
Preferences prefs;

// ============================================================
// RELAY DATA
// ============================================================

#define MAX_SCHEDULES 6

struct ScheduleSlot {
  bool enabled;
  uint16_t startMinutes;
  uint16_t stopMinutes;
  uint8_t days; // bit 0=Mon ... bit 6=Sun
};

struct RelayConfig {
  String name;
  String icon;
  bool state;
  bool autoMode;
  bool manualOverride;
  bool emergencyOff;
  ScheduleSlot schedules[MAX_SCHEDULES];
  unsigned long runtimeToday;
  unsigned long stateStartedMillis;
  float wattage;
};

RelayConfig relays[RELAY_COUNT];

// ============================================================
// WIFI DATA
// ============================================================

String wifiSSID;
String wifiPassword;
float electricityTariff = 0.0f; // INR per kWh

// ============================================================
// TIME
// ============================================================

const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.nist.gov";

const long GMT_OFFSET_SEC = 19800;  // IST +5:30
const int DAYLIGHT_OFFSET_SEC = 0;

int lastDay = -1;

// ============================================================
// ACTIVITY LOG
// ============================================================

#define MAX_LOGS 30

String activityLogs[MAX_LOGS];
int logCount = 0;

// ============================================================
// DEFAULT RELAY DATA
// ============================================================

void clearSchedules(int id) {
  for (int j = 0; j < MAX_SCHEDULES; j++) {
    relays[id].schedules[j].enabled = false;
    relays[id].schedules[j].startMinutes = 0;
    relays[id].schedules[j].stopMinutes = 0;
    relays[id].schedules[j].days = 0x7F;
  }
}

void setDefaultRelayData() {
  relays[0].name = "Main Aquarium Light";
  relays[0].icon = "💡";
  relays[0].autoMode = true;
  relays[1].name = "Air Pump";
  relays[1].icon = "💨";
  relays[1].autoMode = true;
  relays[2].name = "Filter / Water Pump";
  relays[2].icon = "🔄";
  relays[2].autoMode = false;
  relays[3].name = "Heater";
  relays[3].icon = "🌡️";
  relays[3].autoMode = false;

  for (int i = 0; i < RELAY_COUNT; i++) {
    relays[i].state = false;
    relays[i].manualOverride = false;
    relays[i].emergencyOff = false;
    relays[i].runtimeToday = 0;
    relays[i].stateStartedMillis = 0;
    relays[i].wattage = 0.0f;
    clearSchedules(i);
  }

  // Preserve the current working defaults as schedule 1.
  relays[0].schedules[0] = {true, 8 * 60, 14 * 60, 0x7F};
  relays[1].schedules[0] = {true, 10 * 60, 16 * 60, 0x7F};
}

// ============================================================
// TIME HELPERS
// ============================================================

bool getCurrentTime(struct tm &timeinfo) {
  return getLocalTime(&timeinfo);
}

int currentMinutes() {

  struct tm timeinfo;

  if (!getCurrentTime(timeinfo)) {
    return -1;
  }

  return (timeinfo.tm_hour * 60) + timeinfo.tm_min;
}

int currentWeekday() {

  struct tm timeinfo;

  if (!getCurrentTime(timeinfo)) {
    return -1;
  }

  // tm_wday:
  // Sunday = 0
  // Monday = 1
  // ...
  // Saturday = 6

  // Convert to:
  // Monday = 0
  // Tuesday = 1
  // ...
  // Sunday = 6

  return (timeinfo.tm_wday + 6) % 7;
}

// ============================================================
// FORMAT TIME
// ============================================================

String formatTime(uint16_t minutes) {

  int hour = minutes / 60;
  int minute = minutes % 60;

  bool pm = hour >= 12;

  int displayHour = hour % 12;

  if (displayHour == 0) {
    displayHour = 12;
  }

  char buffer[20];

  snprintf(
    buffer,
    sizeof(buffer),
    "%02d:%02d %s",
    displayHour,
    minute,
    pm ? "PM" : "AM"
  );

  return String(buffer);
}

// ============================================================
// FORMAT RUNTIME
// ============================================================

String formatRuntime(unsigned long milliseconds) {

  unsigned long totalMinutes = milliseconds / 60000UL;

  unsigned long hours = totalMinutes / 60UL;
  unsigned long minutes = totalMinutes % 60UL;

  if (hours > 0) {
    return String(hours) + "h " + String(minutes) + "m";
  }

  return String(minutes) + "m";
}

// ============================================================
// URL ENCODE / DECODE
// ============================================================

String urlDecode(String input) {

  String output = "";

  for (unsigned int i = 0; i < input.length(); i++) {

    char c = input[i];

    if (c == '+') {
      output += ' ';
    }
    else if (c == '%' && i + 2 < input.length()) {

      String hex = input.substring(i + 1, i + 3);

      char decoded = strtol(hex.c_str(), nullptr, 16);

      output += decoded;

      i += 2;
    }
    else {
      output += c;
    }
  }

  return output;
}

// ============================================================
// ESCAPE JSON
// ============================================================

String jsonEscape(String input) {

  input.replace("\\", "\\\\");
  input.replace("\"", "\\\"");
  input.replace("\n", "\\n");
  input.replace("\r", "\\r");

  return input;
}

// ============================================================
// LOGGING
// ============================================================

void addLog(String message) {

  struct tm timeinfo;

  String timestamp = "";

  if (getCurrentTime(timeinfo)) {

    char buffer[32];

    strftime(
      buffer,
      sizeof(buffer),
      "%d-%m-%Y %H:%M:%S",
      &timeinfo
    );

    timestamp = String(buffer);
  }

  String entry = timestamp + " | " + message;

  if (logCount < MAX_LOGS) {

    activityLogs[logCount] = entry;
    logCount++;
  }
  else {

    for (int i = 0; i < MAX_LOGS - 1; i++) {
      activityLogs[i] = activityLogs[i + 1];
    }

    activityLogs[MAX_LOGS - 1] = entry;
  }

  Serial.println(entry);
}

// ============================================================
// RELAY OUTPUT
// ============================================================

void setRelayHardware(int id, bool on) {

  if (id < 0 || id >= RELAY_COUNT) {
    return;
  }

  digitalWrite(
    relayPins[id],
    on ? RELAY_ON : RELAY_OFF
  );
}

// ============================================================
// RELAY STATE
// ============================================================

void setRelayState(int id, bool on, String reason) {

  if (id < 0 || id >= RELAY_COUNT) {
    return;
  }

  RelayConfig &r = relays[id];

  if (r.state == on) {
    return;
  }

  // Add runtime before changing state
  if (r.state) {

    unsigned long elapsed =
      millis() - r.stateStartedMillis;

    r.runtimeToday += elapsed;
  }

  r.state = on;

  if (on) {
    r.stateStartedMillis = millis();
  }
  else {
    r.stateStartedMillis = 0;
  }

  setRelayHardware(id, on);

  String action = on ? "ON" : "OFF";

  addLog(
    r.name +
    " " +
    action +
    " (" +
    reason +
    ")"
  );
}

// ============================================================
// CURRENT RUNTIME
// ============================================================

unsigned long getCurrentRuntime(int id) {

  if (id < 0 || id >= RELAY_COUNT) {
    return 0;
  }

  unsigned long runtime = relays[id].runtimeToday;

  if (relays[id].state) {

    runtime += millis() - relays[id].stateStartedMillis;
  }

  return runtime;
}

// ============================================================
// CHECK SCHEDULE
// ============================================================

bool scheduleSlotActive(const ScheduleSlot &slot, int nowMinutes, int today) {
  if (!slot.enabled || slot.days == 0) return false;

  uint16_t start = slot.startMinutes;
  uint16_t stop = slot.stopMinutes;

  if (start == stop) return false; // zero-length schedule

  if (start < stop) {
    return (slot.days & (1 << today)) && nowMinutes >= start && nowMinutes < stop;
  }

  // Overnight: e.g. Monday 22:00 -> Tuesday 06:00.
  if (nowMinutes >= start) {
    return slot.days & (1 << today);
  }
  if (nowMinutes < stop) {
    int previousDay = (today + 6) % 7;
    return slot.days & (1 << previousDay);
  }
  return false;
}

bool isAnyScheduleActive(int id) {
  if (id < 0 || id >= RELAY_COUNT || !relays[id].autoMode) return false;

  int nowMinutes = currentMinutes();
  int today = currentWeekday();
  if (nowMinutes < 0 || today < 0) return false;

  for (int j = 0; j < MAX_SCHEDULES; j++) {
    if (scheduleSlotActive(relays[id].schedules[j], nowMinutes, today)) return true;
  }
  return false;
}

// ============================================================
// SCHEDULE PROCESSING
// ============================================================

void processSchedules() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();

  struct tm timeinfo;
  if (!getCurrentTime(timeinfo)) return;

  int today = currentWeekday();
  if (today < 0) return;

  if (lastDay == -1) lastDay = today;
  if (today != lastDay) {
    for (int i = 0; i < RELAY_COUNT; i++) {
      relays[i].runtimeToday = 0;
      if (relays[i].state) relays[i].stateStartedMillis = millis();
    }
    lastDay = today;
  }

  for (int i = 0; i < RELAY_COUNT; i++) {
    if (!relays[i].autoMode || relays[i].emergencyOff) continue;

    bool scheduledNow = isAnyScheduleActive(i);

    // A manual action outside a schedule is allowed to remain until the
    // next schedule transition. When a schedule becomes active, AUTO wins.
    if (scheduledNow) {
      relays[i].manualOverride = false;
      if (!relays[i].state) setRelayState(i, true, "Schedule");
    } else if (!relays[i].manualOverride) {
      if (relays[i].state) setRelayState(i, false, "Schedule");
    }
  }
}

// ============================================================
// PREFERENCES - SAVE
// ============================================================

void saveRelaySettings(int id) {
  if (id < 0 || id >= RELAY_COUNT) return;
  char key[32];
  snprintf(key, sizeof(key), "name%d", id); prefs.putString(key, relays[id].name);
  snprintf(key, sizeof(key), "icon%d", id); prefs.putString(key, relays[id].icon);
  snprintf(key, sizeof(key), "auto%d", id); prefs.putBool(key, relays[id].autoMode);
  snprintf(key, sizeof(key), "emerg%d", id); prefs.putBool(key, relays[id].emergencyOff);
  snprintf(key, sizeof(key), "watt%d", id); prefs.putFloat(key, relays[id].wattage);
  snprintf(key, sizeof(key), "schedver"); prefs.putUChar(key, 2);
  for (int j = 0; j < MAX_SCHEDULES; j++) {
    snprintf(key, sizeof(key), "e%d_%d", id, j); prefs.putBool(key, relays[id].schedules[j].enabled);
    snprintf(key, sizeof(key), "s%d_%d", id, j); prefs.putUShort(key, relays[id].schedules[j].startMinutes);
    snprintf(key, sizeof(key), "t%d_%d", id, j); prefs.putUShort(key, relays[id].schedules[j].stopMinutes);
    snprintf(key, sizeof(key), "d%d_%d", id, j); prefs.putUChar(key, relays[id].schedules[j].days);
  }
}

// ============================================================
// PREFERENCES - LOAD
// ============================================================

void loadSettings() {
  setDefaultRelayData();
  prefs.begin("aquarium", false);

  wifiSSID = prefs.getString("ssid", DEFAULT_WIFI_SSID);
  wifiPassword = prefs.getString("pass", DEFAULT_WIFI_PASSWORD);
  electricityTariff = prefs.getFloat("tariff", 0.0f);

  uint8_t version = prefs.getUChar("schedver", 0);
  for (int i = 0; i < RELAY_COUNT; i++) {
    char key[32];
    snprintf(key, sizeof(key), "name%d", i); relays[i].name = prefs.getString(key, relays[i].name);
    snprintf(key, sizeof(key), "icon%d", i); relays[i].icon = prefs.getString(key, relays[i].icon);
    snprintf(key, sizeof(key), "auto%d", i); relays[i].autoMode = prefs.getBool(key, relays[i].autoMode);
    snprintf(key, sizeof(key), "emerg%d", i); relays[i].emergencyOff = prefs.getBool(key, false);
    snprintf(key, sizeof(key), "watt%d", i); relays[i].wattage = prefs.getFloat(key, 0.0f);

    if (version >= 2) {
      for (int j = 0; j < MAX_SCHEDULES; j++) {
        snprintf(key, sizeof(key), "e%d_%d", i, j); relays[i].schedules[j].enabled = prefs.getBool(key, relays[i].schedules[j].enabled);
        snprintf(key, sizeof(key), "s%d_%d", i, j); relays[i].schedules[j].startMinutes = prefs.getUShort(key, relays[i].schedules[j].startMinutes);
        snprintf(key, sizeof(key), "t%d_%d", i, j); relays[i].schedules[j].stopMinutes = prefs.getUShort(key, relays[i].schedules[j].stopMinutes);
        snprintf(key, sizeof(key), "d%d_%d", i, j); relays[i].schedules[j].days = prefs.getUChar(key, relays[i].schedules[j].days);
      }
    } else {
      // Migrate the old one-schedule format into slot 0.
      snprintf(key, sizeof(key), "start%d", i); uint16_t oldStart = prefs.getUShort(key, relays[i].schedules[0].startMinutes);
      snprintf(key, sizeof(key), "stop%d", i); uint16_t oldStop = prefs.getUShort(key, relays[i].schedules[0].stopMinutes);
      snprintf(key, sizeof(key), "days%d", i); uint8_t oldDays = prefs.getUChar(key, relays[i].schedules[0].days);
      relays[i].schedules[0].startMinutes = oldStart;
      relays[i].schedules[0].stopMinutes = oldStop;
      relays[i].schedules[0].days = oldDays;
      relays[i].schedules[0].enabled = (oldStart != oldStop);
      saveRelaySettings(i);
    }
  }
}

// ============================================================
// WIFI
// ============================================================

void connectWiFi() {

  Serial.println();
  Serial.println("Connecting to Wi-Fi...");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    wifiSSID.c_str(),
    wifiPassword.c_str()
  );

  int attempts = 0;

  while (
    WiFi.status() != WL_CONNECTED &&
    attempts < 40
  ) {

    delay(500);

    Serial.print(".");

    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("Wi-Fi connected!");
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());

    configTime(
      GMT_OFFSET_SEC,
      DAYLIGHT_OFFSET_SEC,
      NTP_SERVER_1,
      NTP_SERVER_2
    );

    Serial.println("Waiting for time synchronization...");

    struct tm timeinfo;

    if (getLocalTime(&timeinfo, 10000)) {

      Serial.println("Time synchronized.");

      char buffer[64];

      strftime(
        buffer,
        sizeof(buffer),
        "%d-%m-%Y %H:%M:%S",
        &timeinfo
      );

      Serial.print("Current IST time: ");
      Serial.println(buffer);
    }
  }
  else {

    Serial.println("Wi-Fi connection failed.");
  }
}

// ============================================================
// JSON STATUS
// ============================================================

String buildStatusJSON() {
  String json = "{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"relays\":[";

  for (int i = 0; i < RELAY_COUNT; i++) {
    RelayConfig &r = relays[i];
    if (i > 0) json += ",";
    json += "{";
    json += "\"id\":" + String(i + 1) + ",";
    json += "\"name\":\"" + jsonEscape(r.name) + "\",";
    json += "\"icon\":\"" + jsonEscape(r.icon) + "\",";
    json += "\"state\":" + String(r.state ? "true" : "false") + ",";
    json += "\"mode\":\"" + String(r.autoMode ? "AUTO" : "MANUAL") + "\",";
    json += "\"emergency\":" + String(r.emergencyOff ? "true" : "false") + ",";
    json += "\"manualOverride\":" + String(r.manualOverride ? "true" : "false") + ",";
    json += "\"runtime\":" + String(getCurrentRuntime(i)) + ",";
    json += "\"schedules\":[";
    for (int j = 0; j < MAX_SCHEDULES; j++) {
      if (j > 0) json += ",";
      ScheduleSlot &slot = r.schedules[j];
      json += "{\"enabled\":" + String(slot.enabled ? "true" : "false") + ",";
      json += "\"start\":" + String(slot.startMinutes) + ",";
      json += "\"stop\":" + String(slot.stopMinutes) + ",";
      json += "\"days\":" + String(slot.days) + "}";
    }
    json += "]}";
  }
  json += "]}";
  return json;
}

// ============================================================
// API: POWER SETTINGS
// ============================================================

void handlePowerSettings() {
  if (server.method() != HTTP_POST ||
      !server.hasArg("tariff")) {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }

  float tariff = server.arg("tariff").toFloat();
  if (tariff < 0.0f || tariff > 100000.0f) {
    server.send(400, "text/plain", "Invalid tariff");
    return;
  }

  for (int i = 0; i < RELAY_COUNT; i++) {
    String argName = "watt" + String(i + 1);
    if (!server.hasArg(argName)) {
      server.send(400, "text/plain", "Missing wattage");
      return;
    }

    float watts = server.arg(argName).toFloat();
    if (watts < 0.0f || watts > 5000.0f) {
      server.send(400, "text/plain", "Invalid wattage");
      return;
    }
    relays[i].wattage = watts;
  }

  electricityTariff = tariff;
  prefs.putFloat("tariff", electricityTariff);

  for (int i = 0; i < RELAY_COUNT; i++) {
    char key[32];
    snprintf(key, sizeof(key), "watt%d", i);
    prefs.putFloat(key, relays[i].wattage);
  }

  addLog("Power settings updated");
  server.send(200, "application/json", buildStatusJSON());
}

// ============================================================
// POWER CALCULATIONS
// ============================================================

float getTodayEnergyWh(int id) {
  if (id < 0 || id >= RELAY_COUNT) return 0.0f;
  return (getCurrentRuntime(id) / 3600000.0f) * relays[id].wattage;
}

float getTodayTotalEnergyWh() {
  float total = 0.0f;
  for (int i = 0; i < RELAY_COUNT; i++) total += getTodayEnergyWh(i);
  return total;
}

float getTodayCost() {
  return (getTodayTotalEnergyWh() / 1000.0f) * electricityTariff;
}

// ============================================================
// HTML PAGE
// ============================================================

const char INDEX_HTML[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html lang="en">

<head>

<meta charset="UTF-8">

<meta
name="viewport"
content="width=device-width, initial-scale=1.0">

<title>Aquarium Control – Smart Management System</title>

<style>

:root {

  --bg:#eef7fc;
  --card:#ffffff;
  --text:#102a43;
  --muted:#58708a;
  --border:#cfe1ee;

  --blue:#1683e8;
  --green:#08b95c;
  --red:#f23845;
  --yellow:#f0b800;
  --purple:#8c4cff;

  --shadow:
    0 5px 18px rgba(20,70,100,.10);
}

body.dark {

  --bg:#081923;
  --card:#0d2430;
  --text:#f2f8fc;
  --muted:#9db5c5;
  --border:#214658;

  --shadow:
    0 5px 18px rgba(0,0,0,.30);
}

* {
  box-sizing:border-box;
}

body {

  margin:0;

  background:
    linear-gradient(
      135deg,
      var(--bg),
      #ffffff
    );

  color:var(--text);

  font-family:
    Arial,
    Helvetica,
    sans-serif;

  transition:
    background .25s,
    color .25s;
}

body.dark {

  background:
    linear-gradient(
      135deg,
      #06151e,
      #0a202c
    );
}

button,
input,
select {

  font-family:inherit;
}

button {

  cursor:pointer;
}

/* =========================================================
   MAIN CONTAINER
   ========================================================= */

.container {

  max-width:1500px;

  margin:auto;

  padding:
    14px
    18px
    24px;
}

/* =========================================================
   HEADER
   ========================================================= */

.header {

  display:flex;

  align-items:center;

  justify-content:space-between;

  gap:15px;

  margin-bottom:12px;
}

.brand {

  display:flex;

  align-items:center;

  gap:12px;
}

.brand-icon {

  font-size:46px;

  line-height:1;
}

.brand h1 {

  margin:0;

  font-size:
    clamp(24px,3vw,38px);

  color:#0b3766;
}

body.dark .brand h1 {
  color:#eaf6ff;
}

.brand p {

  margin:
    2px
    0
    0;

  color:var(--muted);

  font-size:15px;
}

.top-controls {

  display:flex;

  align-items:center;

  gap:8px;
}

.theme-btn,
.settings-btn {

  border:
    1px solid
    var(--border);

  background:
    var(--card);

  color:var(--text);

  border-radius:12px;

  padding:
    10px
    15px;

  font-size:14px;
}

/* =========================================================
   INFO BAR
   ========================================================= */

.info-bar {

  display:grid;

  grid-template-columns:
    1.2fr
    1.2fr
    1fr
    auto;

  gap:0;

  background:var(--card);

  border:
    1px solid
    var(--border);

  border-radius:15px;

  box-shadow:var(--shadow);

  margin-bottom:14px;

  overflow:hidden;
}

.info {

  padding:
    12px
    18px;

  border-right:
    1px solid
    var(--border);
}

.info:last-child {
  border-right:none;
}

.info-title {

  font-size:13px;

  font-weight:bold;

  color:var(--muted);

  margin-bottom:4px;
}

.info-main {

  font-size:18px;

  font-weight:bold;
}

.info-small {

  font-size:12px;

  color:var(--muted);

  margin-top:2px;
}

.info-actions {

  display:flex;

  align-items:center;

  gap:8px;

  padding:10px;
}

.all-off {

  background:var(--red);

  color:white;

  border:0;

  border-radius:9px;

  padding:
    13px
    18px;

  font-weight:bold;
}

.refresh {

  background:var(--card);

  color:var(--text);

  border:
    1px solid
    var(--border);

  border-radius:9px;

  padding:
    12px
    16px;
}

/* =========================================================
   DASHBOARD SUMMARY
   ========================================================= */

.summary-grid {
  display:grid;
  grid-template-columns:repeat(4,1fr);
  gap:12px;
  margin-bottom:14px;
}

.summary-card {
  background:var(--card);
  border:1px solid var(--border);
  border-radius:14px;
  box-shadow:var(--shadow);
  padding:14px;
  min-width:0;
}

.summary-title {
  color:var(--muted);
  font-size:12px;
  font-weight:bold;
  text-transform:uppercase;
  letter-spacing:.04em;
}

.summary-value {
  margin-top:5px;
  font-size:24px;
  font-weight:800;
}

.summary-detail {
  margin-top:3px;
  color:var(--muted);
  font-size:12px;
  white-space:nowrap;
  overflow:hidden;
  text-overflow:ellipsis;
}

.power-panel {
  margin-top:14px;
}

.power-grid {
  display:grid;
  grid-template-columns:repeat(4,1fr);
  gap:10px;
}

.power-field label {
  display:block;
  color:var(--muted);
  font-size:12px;
  margin-bottom:5px;
}

.power-field input {
  width:100%;
  padding:9px 10px;
  border:1px solid var(--border);
  border-radius:8px;
  background:var(--card);
  color:var(--text);
}

.power-total {
  display:flex;
  justify-content:space-between;
  align-items:center;
  gap:10px;
  margin-top:12px;
  padding-top:10px;
  border-top:1px solid var(--border);
  color:var(--muted);
  font-size:13px;
}

.power-total strong {
  color:var(--text);
}

@media(max-width:900px) {
  .summary-grid {
    grid-template-columns:repeat(2,1fr);
  }

  .power-grid {
    grid-template-columns:repeat(2,1fr);
  }
}

@media(max-width:600px) {
  .summary-grid {
    grid-template-columns:1fr 1fr;
    gap:8px;
  }

  .summary-card {
    padding:11px;
  }

  .summary-value {
    font-size:20px;
  }

  .power-grid {
    grid-template-columns:1fr 1fr;
  }
}

/* =========================================================
   RELAY GRID
   ========================================================= */

.relay-grid {

  display:grid;

  grid-template-columns:
    1fr
    1fr;

  gap:14px;
}

/* =========================================================
   RELAY CARD
   ========================================================= */

.relay {

  background:var(--card);

  border:
    1px solid
    var(--border);

  border-left:
    5px solid
    var(--blue);

  border-radius:15px;

  padding:14px;

  box-shadow:var(--shadow);

  min-width:0;
}

.relay:nth-child(1) {
  border-left-color:#00b95a;
}

.relay:nth-child(2) {
  border-left-color:#159bea;
}

.relay:nth-child(3) {
  border-left-color:#f0bd00;
}

.relay:nth-child(4) {
  border-left-color:#9a4cff;
}

.relay-head {

  display:flex;

  align-items:center;

  gap:10px;
}

.device-icon {

  width:48px;
  height:48px;

  border-radius:12px;

  display:flex;

  align-items:center;

  justify-content:center;

  font-size:28px;

  background:#eaf5fc;
}

body.dark .device-icon {
  background:#102f3d;
}

.device-title {

  flex:1;

  min-width:0;
}

.device-title h2 {

  margin:0;

  font-size:18px;

  white-space:nowrap;

  overflow:hidden;

  text-overflow:ellipsis;
}

.device-title p {

  margin:
    2px
    0
    0;

  color:var(--muted);

  font-size:13px;
}

.switch {

  position:relative;

  width:55px;
  height:31px;

  border-radius:30px;

  background:#637580;

  flex-shrink:0;

  cursor:pointer;
}

.switch::after {

  content:"";

  position:absolute;

  width:23px;
  height:23px;

  top:4px;
  left:4px;

  border-radius:50%;

  background:white;

  transition:.2s;
}

.switch.on {

  background:var(--green);
}

.switch.on::after {

  left:28px;
}

/* =========================================================
   STATUS
   ========================================================= */

.status {

  margin-top:9px;

  font-size:13px;

  color:var(--muted);
}

.status.on {
  color:var(--green);
}

.status.off {
  color:var(--red);
}

.runtime {

  display:flex;

  align-items:center;

  justify-content:space-between;

  gap:8px;

  background:#f1f8fc;

  border:
    1px solid
    var(--border);

  border-radius:9px;

  padding:
    8px
    10px;

  margin-top:8px;

  font-size:13px;
}

body.dark .runtime {
  background:#102b38;
}

.schedule-text {

  font-weight:bold;
}

.next-action {

  color:var(--muted);

  font-size:12px;
}

/* =========================================================
   PROGRESS
   ========================================================= */

.progress {

  height:6px;

  border-radius:10px;

  background:#d8e5ec;

  overflow:hidden;

  margin:
    7px
    0
    8px;
}

.progress span {

  display:block;

  height:100%;

  background:
    linear-gradient(
      90deg,
      #08b95c,
      #1683e8
    );

  border-radius:10px;
}

/* =========================================================
   DAYS
   ========================================================= */

.days {

  display:flex;

  gap:7px;

  flex-wrap:wrap;

  margin:
    6px
    0
    10px;
}

.day {

  width:29px;
  height:29px;

  border-radius:50%;

  border:none;

  background:#dce6ed;

  color:#39566b;

  font-size:11px;

  font-weight:bold;
}

.day.active {

  background:var(--green);

  color:white;
}

/* =========================================================
   BUTTONS
   ========================================================= */

.actions {

  display:grid;

  grid-template-columns:
    1fr
    1fr
    1fr;

  gap:7px;
}

.btn {

  border:0;

  border-radius:8px;

  padding:
    10px
    8px;

  font-weight:bold;

  color:white;
}

.btn-on {
  background:#06b957;
}

.btn-off {
  background:#f23845;
}

.btn-schedule {

  background:var(--card);

  color:var(--text);

  border:
    1px solid
    var(--border);
}

/* =========================================================
   LOWER GRID
   ========================================================= */

.lower-grid {

  display:grid;

  grid-template-columns:
    1fr
    1fr;

  gap:14px;

  margin-top:14px;
}

.panel {

  background:var(--card);

  border:
    1px solid
    var(--border);

  border-radius:15px;

  padding:15px;

  box-shadow:var(--shadow);
}

.panel h3 {

  margin:
    0
    0
    10px;
}

.quick {

  display:flex;

  gap:8px;

  flex-wrap:wrap;
}

.quick button {

  padding:
    10px
    13px;

  background:var(--card);

  color:var(--text);

  border:
    1px solid
    var(--border);

  border-radius:8px;
}

/* =========================================================
   LOG
   ========================================================= */

.log {

  margin-top:14px;

  background:var(--card);

  border:
    1px solid
    var(--border);

  border-radius:15px;

  padding:15px;

  box-shadow:var(--shadow);
}

.log-head {

  display:flex;

  align-items:center;

  justify-content:space-between;
}

.log-content {

  margin-top:10px;

  max-height:180px;

  overflow:auto;

  font-size:12px;

  color:var(--muted);

  line-height:1.7;
}

.clear {

  background:var(--card);

  color:var(--text);

  border:
    1px solid
    var(--border);

  border-radius:8px;

  padding:
    7px
    12px;
}

/* =========================================================
   FOOTER
   ========================================================= */

.footer {

  display:flex;

  justify-content:space-between;

  gap:10px;

  margin-top:12px;

  color:var(--muted);

  font-size:12px;
}

/* =========================================================
   MODAL
   ========================================================= */

.modal {

  display:none;

  position:fixed;

  inset:0;

  background:
    rgba(0,0,0,.55);

  z-index:1000;

  align-items:center;

  justify-content:center;

  padding:15px;
}

.modal.show {
  display:flex;
}

.modal-box {

  width:
    min(500px,100%);

  max-height:90vh;

  overflow:auto;

  background:var(--card);

  color:var(--text);

  border-radius:15px;

  padding:20px;

  box-shadow:
    0 15px 50px
    rgba(0,0,0,.3);
}

.modal-box h2 {
  margin-top:0;
}

.form-group {

  margin:
    12px
    0;
}

.form-group label {

  display:block;

  font-size:13px;

  color:var(--muted);

  margin-bottom:5px;
}

.form-group input,
.form-group select {

  width:100%;

  padding:11px;

  border:
    1px solid
    var(--border);

  border-radius:8px;

  background:var(--card);

  color:var(--text);
}

.modal-actions {

  display:flex;

  gap:8px;

  margin-top:15px;
}

.modal-actions button {

  flex:1;

  padding:11px;

  border-radius:8px;

  border:0;

  font-weight:bold;
}

.save {
  background:var(--blue);
  color:white;
}

.cancel {

  background:
    #dfe8ed;

  color:#19354a;
}

.week-select {

  display:flex;

  gap:7px;

  flex-wrap:wrap;

  margin-top:8px;
}

.week-select button {

  width:35px;
  height:35px;

  border-radius:50%;

  border:0;

  background:#dce6ed;

  color:#39566b;

  font-weight:bold;
}

.week-select button.active {

  background:var(--green);

  color:white;
}

/* =========================================================
   MOBILE
   ========================================================= */

@media(max-width:800px) {

  .container {
    padding:10px;
  }

  .header {
    align-items:flex-start;
  }

  .brand-icon {
    font-size:34px;
  }

  .brand h1 {
    font-size:22px;
  }

  .brand p {
    font-size:12px;
  }

  .theme-btn {
    font-size:12px;
    padding:8px;
  }

  .settings-btn {
    padding:8px 10px;
  }

  .info-bar {

    grid-template-columns:
      1fr
      1fr;

    gap:0;
  }

  .info {

    border-right:none;

    border-bottom:
      1px solid
      var(--border);

    padding:9px 11px;
  }

  .info-actions {

    grid-column:
      1 / -1;

    border-top:
      1px solid
      var(--border);

    justify-content:stretch;
  }

  .info-actions button {
    flex:1;
  }

  .relay-grid {

    grid-template-columns:1fr;

    gap:10px;
  }

  .relay {

    padding:11px;
  }

  .device-icon {

    width:42px;
    height:42px;

    font-size:24px;
  }

  .device-title h2 {
    font-size:16px;
  }

  .actions {
    grid-template-columns:
      1fr
      1fr
      1fr;
  }

  .btn {
    font-size:12px;
    padding:9px 5px;
  }

  .lower-grid {

    grid-template-columns:1fr;

    gap:10px;
  }

  .panel {
    padding:12px;
  }

  .footer {
    flex-direction:column;
  }
}

/* =========================================================
   VERY SMALL PHONE
   ========================================================= */

@media(max-width:430px) {

  .brand p {
    display:none;
  }

  .top-controls {
    gap:4px;
  }

  .theme-btn {
    font-size:0;
  }

  .theme-btn::before {
    content:"☀️";
    font-size:18px;
  }

  .schedule-text {
    font-size:12px;
  }

  .next-action {
    display:none;
  }

  .day {

    width:27px;
    height:27px;
  }
}

</style>

</head>

<body>

<div class="container">

<!-- ========================================================
     HEADER
     ======================================================== -->

<header class="header">

  <div class="brand">

    <div class="brand-icon">
      🐠
    </div>

    <div>

      <h1>
        Aquarium Control – Smart Management System
      </h1>

      <p>
        Smart Control for a Healthier Aquarium
      </p>

    </div>

  </div>

  <div class="top-controls">

    <button
      class="theme-btn"
      onclick="toggleTheme()">

      ☀️ Light / 🌙 Dark

    </button>

    <button
      class="settings-btn"
      onclick="wifiSettings()">

      ⚙️

    </button>

  </div>

</header>


<!-- ========================================================
     INFO BAR
     ======================================================== -->

<section class="info-bar">

  <div class="info">

    <div class="info-title">
      🕐 Current Time (IST)
    </div>

    <div
      class="info-main"
      id="clock">
      --:--:--
    </div>

    <div
      class="info-small"
      id="date">
      --
    </div>

  </div>


  <div class="info">

    <div class="info-title">
      📶 ESP32
    </div>

    <div
      class="info-main"
      id="connection">
      Checking...
    </div>

    <div
      class="info-small"
      id="ip">
      --
    </div>

  </div>


  <div class="info">

    <div class="info-title">
      🌡️ Room Temp
    </div>

    <div class="info-main">
      Sensor not connected
    </div>

    <div class="info-small">
      Ready for future sensor
    </div>

  </div>


  <div class="info-actions">

    <button
      class="all-off"
      onclick="allOff()">

      ⏻ All OFF

    </button>

    <button
      class="refresh"
      onclick="loadStatus()">

      ↻ Refresh

    </button>

  </div>

</section>


<!-- ========================================================
     DASHBOARD SUMMARY
     ======================================================== -->

<section class="summary-grid">

  <div class="summary-card">
    <div class="summary-title">Active Relays</div>
    <div class="summary-value" id="summaryActive">0 / 4</div>
    <div class="summary-detail" id="summaryActiveDetail">All relays OFF</div>
  </div>

  <div class="summary-card">
    <div class="summary-title">Today's Runtime</div>
    <div class="summary-value" id="summaryRuntime">0m</div>
    <div class="summary-detail" id="summaryRuntimeDetail">Across all relays</div>
  </div>

  <div class="summary-card">
    <div class="summary-title">Estimated Power Usage</div>
    <div class="summary-value" id="summaryPower">0 W</div>
    <div class="summary-detail" id="summaryEnergy">0 Wh today</div>
  </div>

  <div class="summary-card">
    <div class="summary-title">Next Scheduled</div>
    <div class="summary-value" id="summaryNext">None</div>
    <div class="summary-detail" id="summaryNextDetail">No upcoming schedule</div>
  </div>

</section>


<!-- ========================================================
     RELAYS
     ======================================================== -->

<section
  class="relay-grid"
  id="relayGrid">

</section>


<!-- ========================================================
     LOWER PANELS
     ======================================================== -->

<section class="lower-grid">

  <div class="panel">

    <h3>
      ⚙️ Quick Settings
    </h3>

    <div class="quick">

      <button onclick="openNames()">
        ✏️ Device Names & Icons
      </button>

      <button onclick="openDefaults()">
        🕐 Default Schedules
      </button>

      <button onclick="wifiSettings()">
        📶 Wi-Fi Settings
      </button>

    </div>

  </div>


  <div class="panel">

    <h3>
      🛡️ System Status
    </h3>

    <div
      id="systemStatus"
      style="color:#08b95c">

      ● Checking system...

    </div>

  </div>

</section>


<!-- ========================================================
     POWER SETTINGS
     ======================================================== -->

<section class="panel power-panel">

  <h3>⚡ Power Settings</h3>

  <p style="margin-top:0;color:var(--muted);font-size:12px">
    Enter the approximate wattage of each connected device. Values are
    stored in the ESP32 and used for dashboard estimates.
  </p>

  <div class="power-grid">

    <div class="power-field">
      <label>Relay 1 Wattage (W)</label>
      <input id="wattage1" type="number" min="0" step="1" value="0"
             onchange="savePowerSettings()">
    </div>

    <div class="power-field">
      <label>Relay 2 Wattage (W)</label>
      <input id="wattage2" type="number" min="0" step="1" value="0"
             onchange="savePowerSettings()">
    </div>

    <div class="power-field">
      <label>Relay 3 Wattage (W)</label>
      <input id="wattage3" type="number" min="0" step="1" value="0"
             onchange="savePowerSettings()">
    </div>

    <div class="power-field">
      <label>Relay 4 Wattage (W)</label>
      <input id="wattage4" type="number" min="0" step="1" value="0"
             onchange="savePowerSettings()">
    </div>

  </div>

  <div class="power-grid" style="margin-top:10px;grid-template-columns:1fr 3fr">

    <div class="power-field">
      <label>Electricity Tariff (₹ / kWh)</label>
      <input id="tariff" type="number" min="0" step="0.01" value="0"
             onchange="savePowerSettings()">
    </div>

    <div class="power-field">
      <label>Estimate</label>
      <div class="power-total">
        <span>Today's estimated cost</span>
        <strong id="summaryCost">₹0.00</strong>
      </div>
    </div>

  </div>

</section>


<!-- ========================================================
     ACTIVITY LOG
     ======================================================== -->

<section class="log">

  <div class="log-head">

    <h3>
      📝 Activity Log
    </h3>

    <button
      class="clear"
      onclick="clearLogs()">

      Clear

    </button>

  </div>

  <div
    class="log-content"
    id="logs">

    Loading...

  </div>

</section>


<!-- ========================================================
     FOOTER
     ======================================================== -->

<footer class="footer">

  <div>
    🐠 Aquarium Controller v2.0.1
  </div>

  <div id="footerConnection">
    ESP32
  </div>

</footer>

</div>


<!-- ========================================================
     SCHEDULE MODAL
     ======================================================== -->

<div class="modal" id="scheduleModal">
  <div class="modal-box">
    <h2 id="scheduleTitle">📅 Schedules</h2>
    <input type="hidden" id="scheduleId">
    <div class="form-group">
      <label>Relay Mode</label>
      <select id="scheduleMode">
        <option value="AUTO">AUTO</option>
        <option value="MANUAL">MANUAL</option>
      </select>
    </div>
    <div id="scheduleList"></div>
    <div class="modal-actions">
      <button class="cancel" onclick="closeModal('scheduleModal')">Close</button>
    </div>
  </div>
</div>


<!-- ========================================================
     NAMES MODAL
     ======================================================== -->

<div
  class="modal"
  id="namesModal">

  <div class="modal-box">

    <h2>
      ✏️ Device Names & Icons
    </h2>

    <div id="nameFields"></div>

    <div class="modal-actions">

      <button
        class="cancel"
        onclick="closeModal('namesModal')">

        Close

      </button>

    </div>

  </div>

</div>


<!-- ========================================================
     WIFI MODAL
     ======================================================== -->

<div
  class="modal"
  id="wifiModal">

  <div class="modal-box">

    <h2>
      📶 Wi-Fi Settings
    </h2>

    <p
      style="color:var(--muted);font-size:13px">

      Update your Wi-Fi without re-uploading
      the ESP32 code.

    </p>


    <div class="form-group">

      <label>
        Wi-Fi Name (SSID)
      </label>

      <input
        id="wifiSSID"
        autocomplete="off">

    </div>


    <div class="form-group">

      <label>
        Wi-Fi Password
      </label>

      <input
        type="password"
        id="wifiPassword"
        autocomplete="off">

    </div>


    <div class="modal-actions">

      <button
        class="cancel"
        onclick="closeModal('wifiModal')">

        Cancel

      </button>

      <button
        class="save"
        onclick="saveWiFi()">

        💾 Save Wi-Fi Settings

      </button>

    </div>

  </div>

</div>


<script>

let DATA = null;

const DAYS = [
  "M",
  "T",
  "W",
  "T",
  "F",
  "S",
  "S"
];

let selectedDays = 127;


// ==========================================================
// ELEMENT
// ==========================================================

function $(id) {
  return document.getElementById(id);
}


// ==========================================================
// LOAD STATUS
// ==========================================================

function loadStatus() {

  fetch("/api/status")

  .then(response => response.json())

  .then(data => {

    DATA = data;

    render();

  })

  .catch(() => {

    $("connection").textContent =
      "Disconnected";

    $("systemStatus").innerHTML =
      "<span style='color:#f23845'>● ESP32 disconnected</span>";

  });


  fetch("/api/logs")

  .then(response => response.json())

  .then(data => {

    if (!data.logs.length) {

      $("logs").innerHTML =
        "No activity yet.";

      return;
    }

    $("logs").innerHTML =
      data.logs
      .slice()
      .reverse()
      .map(x => escapeHtml(x))
      .join("<br>");

  });

}


// ==========================================================
// POWER / DASHBOARD HELPERS
// ==========================================================

const POWER_DEFAULTS = [0, 0, 0, 0];

function getPowerSettings() {
  if (DATA && Array.isArray(DATA.relays)) {
    return {
      watts: DATA.relays.map(r => Number(r.wattage || 0)),
      tariff: Number(DATA.tariff || 0)
    };
  }

  return { watts: POWER_DEFAULTS.slice(), tariff: 0 };
}

function loadPowerSettings() {
  let settings = getPowerSettings();

  settings.watts.forEach((value, index) => {
    let input = $("wattage" + (index + 1));
    if (input) input.value = value;
  });

  let tariffInput = $("tariff");
  if (tariffInput) tariffInput.value = settings.tariff;
}

function savePowerSettings() {
  let params = new URLSearchParams();

  for (let i = 0; i < 4; i++) {
    let input = $("wattage" + (i + 1));
    let value = Number(input ? input.value : 0);
    if (!Number.isFinite(value) || value < 0) value = 0;
    params.append("watt" + (i + 1), value);
  }

  let tariff = Number($("tariff") ? $("tariff").value : 0);
  if (!Number.isFinite(tariff) || tariff < 0) tariff = 0;
  params.append("tariff", tariff);

  fetch("/api/power", {
    method: "POST",
    headers: {
      "Content-Type": "application/x-www-form-urlencoded"
    },
    body: params.toString()
  })
  .then(response => {
    if (!response.ok) throw new Error("Power settings failed");
    return response.json();
  })
  .then(data => {
    DATA = data;
    render();
  })
  .catch(() => {
    $("systemStatus").innerHTML =
      "<span style='color:#f23845'>● Failed to save power settings</span>";
  });
}

function formatTotalRuntime(ms) {
  let minutes = Math.floor(ms / 60000);
  let hours = Math.floor(minutes / 60);
  minutes %= 60;

  if (hours > 0) return hours + "h " + minutes + "m";
  return minutes + "m";
}

function getNextSchedule() {
  if (!DATA || !DATA.relays) return null;

  const now = new Date();
  const currentDay = (now.getDay() + 6) % 7;
  const currentMinutes = now.getHours() * 60 + now.getMinutes();

  let best = null;

  DATA.relays.forEach(relay => {
    relay.schedules.forEach((slot, slotIndex) => {
      if (!slot.enabled || !slot.days) return;

      for (let offset = 0; offset < 8; offset++) {
        let day = (currentDay + offset) % 7;
        let start = Number(slot.start);

        if (!(slot.days & (1 << day))) continue;
        if (offset === 0 && start <= currentMinutes) continue;

        let date = new Date(now);
        date.setDate(now.getDate() + offset);
        date.setHours(Math.floor(start / 60), start % 60, 0, 0);

        if (!best || date < best.date) {
          best = { relay, slot: slotIndex, date };
        }
      }
    });
  });

  return best;
}

// ==========================================================
// RENDER
// ==========================================================

function render() {

  if (!DATA) return;


  $("connection").textContent =
    DATA.wifi
      ? "Connected"
      : "Disconnected";


  $("ip").textContent =
    DATA.ip;


  $("footerConnection").textContent =
    "ESP32 " +
    (DATA.wifi ? "Connected" : "Disconnected") +
    " | " +
    DATA.ip;


  let anyEmergency = DATA.relays.some(r => r.emergency);
  $("systemStatus").innerHTML = DATA.wifi
    ? (anyEmergency
        ? "<b style='color:#f23845'>● Emergency OFF active</b><br>" +
          "One or more relays are isolated from schedules. <button onclick=\"resumeAll()\">Resume AUTO</button>"
        : "<b style='color:#08b95c'>● All systems normal</b><br>" +
          "4 devices configured • Wi-Fi connected • Schedules active")
    : "<b style='color:#f23845'>● Wi-Fi disconnected</b>";


  let html = "";


  DATA.relays.forEach((relay, index) => {

    let runtime =
      formatRuntime(relay.runtime);


    let progress = Math.min(
      100,
      (relay.runtime / 21600000) * 100
    );


    let activeSchedules = relay.schedules.filter(s => s.enabled);
    let scheduleText = relay.mode === "AUTO"
      ? (activeSchedules.length ? activeSchedules.map(s => minutesToTime(s.start) + " → " + minutesToTime(s.stop)).join(" • ") : "No schedules")
      : "Manual control";
    let nextText = relay.emergency ? "🚨 Emergency OFF" : (relay.manualOverride ? "Manual override" : "");


    let daysHTML = "";


    for (let d = 0; d < 7; d++) {

      let dayMask = relay.schedules.length ? relay.schedules[0].days : 127;
      daysHTML += `<button class='day ${dayMask & (1 << d) ? "active" : ""}' onclick='toggleDay(${relay.id},${d})'>${DAYS[d]}</button>`;
    }


    html += `

      <article class="relay">

        <div class="relay-head">

          <div class="device-icon">
            ${escapeHtml(relay.icon)}
          </div>

          <div class="device-title">

            <h2>
              ${escapeHtml(relay.name)}
            </h2>

            <p>
              Relay ${relay.id}
              •
              ${relay.mode} mode
            </p>

          </div>

          <div

            class="switch ${relay.state ? "on" : ""}"

            onclick="toggleRelay(
              ${relay.id},
              ${relay.state ? "off" : "on"}
            )">

          </div>

        </div>


        <div

          class="status ${relay.state ? "on" : "off"}">

          ${relay.state ? "● ON" : "● OFF"}
          •
          Today ${runtime}

        </div>


        <div class="runtime">

          <span class="schedule-text">
            ${scheduleText}
          </span>

          <span class="next-action">
            ${nextText}
          </span>

        </div>


        <div class="progress">

          <span
            style="width:${progress}%">
          </span>

        </div>


        <div class="days">

          ${daysHTML}

        </div>


        <div class="actions">

          <button

            class="btn btn-on"

            onclick="toggleRelay(
              ${relay.id},
              'on'
            )">

            ▶ On

          </button>


          <button

            class="btn btn-off"

            onclick="toggleRelay(
              ${relay.id},
              'off'
            )">

            ■ Off

          </button>


          <button

            class="btn btn-schedule"

            onclick="openSchedule(
              ${relay.id}
            )">

            📅 Schedule

          </button>

          ${relay.emergency ? `<button class="btn btn-off" onclick="resumeRelay(${relay.id})">▶ Resume AUTO</button>` : `<button class="btn" style="border:1px solid #ff8a8a" onclick="emergencyOff(${relay.id})">🚨 Emergency OFF</button>`}

        </div>

      </article>

    `;

  });


  $("relayGrid").innerHTML = html;
  renderDashboard();
}


// ==========================================================
// FORMAT RUNTIME
// ==========================================================

function formatRuntime(ms) {

  let minutes =
    Math.floor(ms / 60000);

  let hours =
    Math.floor(minutes / 60);

  minutes =
    minutes % 60;


  if (hours > 0) {

    return (
      hours +
      "h " +
      minutes +
      "m"
    );

  }

  return minutes + "m";
}


// ==========================================================
// TOGGLE RELAY
// ==========================================================

function toggleRelay(id, action) {
  fetch("/api/relay?id=" + id + "&action=" + action)
    .then(async response => {
      if (response.ok) return;
      let msg = await response.text();
      if (response.status === 409 && confirm(msg + "\n\nDo you want to use Emergency OFF?")) {
        return fetch("/api/relay?id=" + id + "&action=emergency");
      }
      throw new Error(msg);
    })
    .then(loadStatus)
    .catch(e => alert(e.message || "Unable to control relay."));
}

function emergencyOff(id) {
  if (!confirm("Emergency OFF will force this relay OFF and ignore all schedules until you resume AUTO. Continue?")) return;
  fetch("/api/relay?id=" + id + "&action=emergency").then(loadStatus);
}

function resumeRelay(id) {
  fetch("/api/relay?id=" + id + "&action=resume").then(loadStatus);
}

// ==========================================================
// ALL OFF
// ==========================================================

function allOff() {
  if (!confirm("Emergency OFF will force all four relays OFF and ignore schedules until you resume AUTO. Continue?")) return;
  fetch("/api/alloff").then(loadStatus);
}

function resumeAll() {
  fetch("/api/resumeall").then(loadStatus);
}


// ==========================================================
// SCHEDULE
// ==========================================================

function openSchedule(id) {
  let relay = DATA.relays.find(r => r.id == id);
  if (!relay) return;
  $("scheduleId").value = id;
  $("scheduleTitle").textContent = "📅 " + relay.name + " Schedules";
  $("scheduleMode").value = relay.mode;
  let html = "";
  relay.schedules.forEach((slot, i) => {
    let days = "";
    for (let d = 0; d < 7; d++) {
      days += `<button class="day ${slot.days & (1 << d) ? "active" : ""}" onclick="toggleModalDay(${id},${i},${d})">${DAYS[d]}</button>`;
    }
    html += `<div class="schedule-editor">
      <div style="display:flex;justify-content:space-between;align-items:center;gap:8px">
        <b>Schedule ${i + 1}</b>
        <label><input type="checkbox" id="en_${i}" ${slot.enabled ? "checked" : ""}> Enabled</label>
      </div>
      <div class="form-group"><label>Start Time</label><input type="time" id="st_${i}" value="${minutesToTime(slot.start)}"></div>
      <div class="form-group"><label>Stop Time</label><input type="time" id="sp_${i}" value="${minutesToTime(slot.stop)}"></div>
      <div class="form-group"><label>Active Days</label><div class="week-select" id="wd_${i}">${days}</div></div>
      <button class="save" style="width:100%;padding:9px;border:0;border-radius:8px" onclick="saveSchedule(${id},${i})">💾 Save Schedule ${i + 1}</button>
    </div>`;
  });
  $("scheduleList").innerHTML = html;
  $("scheduleModal").classList.add("show");
}

function minutesToTime(minutes) {
  let h = Math.floor(minutes / 60), m = minutes % 60;
  return String(h).padStart(2,"0") + ":" + String(m).padStart(2,"0");
}

function timeToMinutes(value) {
  let parts = value.split(":");
  return parseInt(parts[0]) * 60 + parseInt(parts[1]);
}

function toggleModalDay(id, slotId, day) {
  let relay = DATA.relays.find(r => r.id == id);
  relay.schedules[slotId].days ^= (1 << day);
  openSchedule(id);
}

function saveSchedule(id, slotId) {
  let relay = DATA.relays.find(r => r.id == id);
  let slot = relay.schedules[slotId];
  let enabled = $("en_" + slotId).checked;
  let start = timeToMinutes($("st_" + slotId).value);
  let stop = timeToMinutes($("sp_" + slotId).value);
  if (enabled && (start === stop || slot.days === 0)) {
    alert("Please select a valid start/stop time and at least one day.");
    return;
  }
  let params = new URLSearchParams();
  params.append("id", id); params.append("slot", slotId); params.append("enabled", enabled ? "1" : "0");
  params.append("start", start); params.append("stop", stop); params.append("days", slot.days);
  params.append("mode", $("scheduleMode").value);
  fetch("/api/schedule", {method:"POST", body:params})
    .then(async r => { if (!r.ok) throw new Error(await r.text()); return r.text(); })
    .then(() => loadStatus())
    .then(() => openSchedule(id))
    .catch(e => alert(e.message || "Unable to save schedule."));
}

// ==========================================================
// QUICK NAMES
// ==========================================================

function openNames() {

  let html = "";


  DATA.relays.forEach(relay => {

    html += `

      <div class="form-group">

        <label>
          Relay ${relay.id} Name
        </label>

        <input
          id="name${relay.id}"
          value="${escapeAttr(relay.name)}">

      </div>


      <div class="form-group">

        <label>
          Relay ${relay.id} Icon
        </label>

        <input
          id="icon${relay.id}"
          value="${escapeAttr(relay.icon)}">

      </div>

      <button
        class="save"
        style="width:100%;padding:10px;border:0;border-radius:8px"
        onclick="saveDevice(${relay.id})">

        💾 Save Relay ${relay.id}

      </button>

    `;

  });


  $("nameFields").innerHTML =
    html;


  $("namesModal")
    .classList
    .add("show");
}


// ==========================================================
// SAVE DEVICE
// ==========================================================

function saveDevice(id) {

  let name =
    $("name" + id).value;


  let icon =
    $("icon" + id).value;


  let params =
    new URLSearchParams();


  params.append(
    "id",
    id
  );


  params.append(
    "name",
    name
  );


  params.append(
    "icon",
    icon
  );


  fetch(
    "/api/device",
    {
      method:"POST",
      body:params
    }
  )

  .then(() => {

    loadStatus();

  });
}


// ==========================================================
// WIFI
// ==========================================================

function wifiSettings() {

  $("wifiSSID").value = "";

  $("wifiPassword").value = "";

  $("wifiModal")
    .classList
    .add("show");
}


function saveWiFi() {

  let ssid =
    $("wifiSSID").value.trim();


  let password =
    $("wifiPassword").value;


  if (!ssid) {

    alert(
      "Please enter Wi-Fi name."
    );

    return;
  }


  let params =
    new URLSearchParams();


  params.append(
    "ssid",
    ssid
  );


  params.append(
    "password",
    password
  );


  fetch(
    "/api/wifi",
    {
      method:"POST",
      body:params
    }
  )

  .then(() => {

    alert(
      "Wi-Fi settings saved. ESP32 will restart."
    );

    closeModal(
      "wifiModal"
    );

  });
}


// ==========================================================
// DEFAULTS
// ==========================================================

function openDefaults() {

  alert(
    "Each relay supports up to 6 AUTO schedules. Use the Schedule button to configure them. MANUAL mode keeps direct control, while Emergency OFF overrides all schedules."
  );
}


// ==========================================================
// DAYS ON MAIN CARD
// ==========================================================

function toggleDay(id, day) {
  let relay = DATA.relays.find(r => r.id == id);
  if (!relay || !relay.schedules.length) return;
  let slot = relay.schedules[0];
  let newDays = slot.days ^ (1 << day);
  let params = new URLSearchParams();
  params.append("id", id); params.append("slot", 0); params.append("enabled", slot.enabled ? "1" : "0");
  params.append("start", slot.start); params.append("stop", slot.stop); params.append("days", newDays); params.append("mode", relay.mode);
  fetch("/api/schedule", {method:"POST", body:params}).then(loadStatus);
}

// ==========================================================
// LOGS
// ==========================================================

function clearLogs() {

  if (
    !confirm(
      "Clear activity log?"
    )
  ) {
    return;
  }


  fetch(
    "/api/logs/clear"
  )

  .then(loadStatus);
}


// ==========================================================
// THEME
// ==========================================================

function toggleTheme() {

  document.body
    .classList
    .toggle("dark");


  localStorage.setItem(
    "aquariumTheme",
    document.body.classList.contains("dark")
      ? "dark"
      : "light"
  );
}


// ==========================================================
// MODAL CLOSE
// ==========================================================

function closeModal(id) {

  $(id)
    .classList
    .remove("show");
}


// ==========================================================
// CLOCK
// ==========================================================

function updateClock() {

  let now =
    new Date();


  $("clock").textContent =
    now.toLocaleTimeString(
      "en-IN",
      {
        hour12:true
      }
    );


  $("date").textContent =
    now.toLocaleDateString(
      "en-IN",
      {
        weekday:"long",
        day:"2-digit",
        month:"short",
        year:"numeric"
      }
    ) +
    " • IST (UTC +5:30)";
}


// ==========================================================
// ESCAPE
// ==========================================================

function escapeHtml(value) {

  return String(value)

    .replaceAll(
      "&",
      "&amp;"
    )

    .replaceAll(
      "<",
      "&lt;"
    )

    .replaceAll(
      ">",
      "&gt;"
    )

    .replaceAll(
      '"',
      "&quot;"
    )

    .replaceAll(
      "'",
      "&#039;"
    );
}


function escapeAttr(value) {

  return escapeHtml(value);
}


// ==========================================================
// INITIALIZATION
// ==========================================================

if (
  localStorage.getItem(
    "aquariumTheme"
  ) === "dark"
) {

  document.body
    .classList
    .add("dark");
}


updateClock();

setInterval(
  updateClock,
  1000
);


loadPowerSettings();
loadStatus();

setInterval(
  loadStatus,
  5000
);

</script>

</body>

</html>

)rawliteral";

// ============================================================
// API: STATUS
// ============================================================

void handleStatus() {

  server.send(
    200,
    "application/json",
    buildStatusJSON()
  );
}

// ============================================================
// API: RELAY
// ============================================================

void handleRelay() {
  if (!server.hasArg("id") || !server.hasArg("action")) { server.send(400, "text/plain", "Missing parameters"); return; }
  int id = server.arg("id").toInt() - 1;
  String action = server.arg("action");
  if (id < 0 || id >= RELAY_COUNT) { server.send(400, "text/plain", "Invalid relay"); return; }

  RelayConfig &r = relays[id];
  bool scheduledNow = r.autoMode && isAnyScheduleActive(id);

  if (r.emergencyOff && action != "resume") {
    server.send(409, "text/plain", "Emergency OFF is active. Resume AUTO first.");
    return;
  }

  if (action == "on" || action == "off") {
    if (r.autoMode && scheduledNow) {
      server.send(409, "text/plain", "An AUTO schedule is active. Use Emergency OFF to force OFF.");
      return;
    }
    r.manualOverride = r.autoMode;
    setRelayState(id, action == "on", "Manual");
    server.send(200, "text/plain", "OK");
    return;
  }

  if (action == "resume") {
    r.emergencyOff = false;
    r.manualOverride = false;
    saveRelaySettings(id);
    if (r.autoMode) {
      bool shouldOn = isAnyScheduleActive(id);
      setRelayState(id, shouldOn, "AUTO Resume");
    }
    addLog(r.name + " AUTO resumed");
    server.send(200, "text/plain", "OK");
    return;
  }

  if (action == "emergency") {
    r.emergencyOff = true;
    r.manualOverride = false;
    setRelayState(id, false, "Emergency OFF");
    saveRelaySettings(id);
    server.send(200, "text/plain", "OK");
    return;
  }

  server.send(400, "text/plain", "Invalid action");
}

// ============================================================
// API: ALL OFF
// ============================================================

void handleAllOff() {
  for (int i = 0; i < RELAY_COUNT; i++) {
    relays[i].emergencyOff = true;
    relays[i].manualOverride = false;
    setRelayState(i, false, "Emergency All OFF");
    saveRelaySettings(i);
  }
  server.send(200, "text/plain", "OK");
}

void handleResumeAll() {
  for (int i = 0; i < RELAY_COUNT; i++) {
    relays[i].emergencyOff = false;
    relays[i].manualOverride = false;
    saveRelaySettings(i);
    if (relays[i].autoMode) {
      setRelayState(i, isAnyScheduleActive(i), "AUTO Resume");
    }
  }
  addLog("All relays AUTO resumed");
  server.send(200, "text/plain", "OK");
}

// ============================================================
// API: SCHEDULE
// ============================================================

void handleSchedule() {
  if (!server.hasArg("id") || !server.hasArg("slot") || !server.hasArg("enabled") ||
      !server.hasArg("start") || !server.hasArg("stop") || !server.hasArg("days")) {
    server.send(400, "text/plain", "Missing parameters"); return;
  }
  int id = server.arg("id").toInt() - 1;
  int slotId = server.arg("slot").toInt();
  if (id < 0 || id >= RELAY_COUNT || slotId < 0 || slotId >= MAX_SCHEDULES) {
    server.send(400, "text/plain", "Invalid relay or schedule slot"); return;
  }
  RelayConfig &r = relays[id];
  ScheduleSlot &slot = r.schedules[slotId];
  uint16_t start = (uint16_t)server.arg("start").toInt();
  uint16_t stop = (uint16_t)server.arg("stop").toInt();
  uint8_t days = (uint8_t)server.arg("days").toInt();
  bool enabled = server.arg("enabled") == "1";
  if (enabled && (start == stop || days == 0)) {
    server.send(400, "text/plain", "Enabled schedule needs different start/stop times and at least one day"); return;
  }
  slot.enabled = enabled; slot.startMinutes = start; slot.stopMinutes = stop; slot.days = days;
  if (server.hasArg("mode")) r.autoMode = server.arg("mode") == "AUTO";
  r.manualOverride = false;
  saveRelaySettings(id);
  addLog(r.name + " schedule " + String(slotId + 1) + " updated");
  server.send(200, "text/plain", "OK");
}

// ============================================================
// API: DEVICE
// ============================================================

void handleDevice() {

  if (
    !server.hasArg("id") ||
    !server.hasArg("name") ||
    !server.hasArg("icon")
  ) {

    server.send(
      400,
      "text/plain",
      "Missing parameters"
    );

    return;
  }

  int id =
    server.arg("id").toInt() - 1;

  if (
    id < 0 ||
    id >= RELAY_COUNT
  ) {

    server.send(
      400,
      "text/plain",
      "Invalid relay"
    );

    return;
  }

  relays[id].name =
    urlDecode(
      server.arg("name")
    );

  relays[id].icon =
    urlDecode(
      server.arg("icon")
    );

  saveRelaySettings(id);

  addLog(
    "Relay " +
    String(id + 1) +
    " name/icon updated"
  );

  server.send(
    200,
    "text/plain",
    "OK"
  );
}

// ============================================================
// API: WIFI
// ============================================================

void handleWiFi() {

  if (
    !server.hasArg("ssid") ||
    !server.hasArg("password")
  ) {

    server.send(
      400,
      "text/plain",
      "Missing Wi-Fi parameters"
    );

    return;
  }

  String newSSID =
    urlDecode(
      server.arg("ssid")
    );

  String newPassword =
    urlDecode(
      server.arg("password")
    );

  if (newSSID.length() == 0) {

    server.send(
      400,
      "text/plain",
      "SSID cannot be empty"
    );

    return;
  }

  prefs.putString(
    "ssid",
    newSSID
  );

  prefs.putString(
    "pass",
    newPassword
  );

  server.send(
    200,
    "text/plain",
    "Wi-Fi saved. Restarting..."
  );

  delay(1000);

  ESP.restart();
}

// ============================================================
// API: LOGS
// ============================================================

void handleLogs() {

  String json =
    "{\"logs\":[";


  for (int i = 0; i < logCount; i++) {

    if (i > 0) {
      json += ",";
    }

    json += "\"";

    json +=
      jsonEscape(
        activityLogs[i]
      );

    json += "\"";
  }


  json += "]}";


  server.send(
    200,
    "application/json",
    json
  );
}

// ============================================================
// API: CLEAR LOGS
// ============================================================

void handleClearLogs() {

  logCount = 0;

  server.send(
    200,
    "text/plain",
    "OK"
  );
}

// ============================================================
// API: RESET
// ============================================================

void handleReset() {

  prefs.clear();

  electricityTariff = 0.0f;
  setDefaultRelayData();

  for (int i = 0; i < RELAY_COUNT; i++) {

    setRelayHardware(
      i,
      false
    );
  }

  server.send(
    200,
    "text/plain",
    "Resetting..."
  );

  delay(1000);

  ESP.restart();
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println(
    "========================================"
  );

  Serial.println(
    "ESP32 Smart Relay Controller"
  );

  Serial.println(
    "========================================"
  );


  // ----------------------------------------------------------
  // Relay pins
  // ----------------------------------------------------------

  for (int i = 0; i < RELAY_COUNT; i++) {

    pinMode(
      relayPins[i],
      OUTPUT
    );

    // IMPORTANT:
    // Active LOW relay = HIGH means OFF

    digitalWrite(
      relayPins[i],
      RELAY_OFF
    );
  }


  // ----------------------------------------------------------
  // Load settings
  // ----------------------------------------------------------

  loadSettings();


  // ----------------------------------------------------------
  // Connect Wi-Fi
  // ----------------------------------------------------------

  connectWiFi();


  // ----------------------------------------------------------
  // OTA
  // ----------------------------------------------------------
  ArduinoOTA.setHostname("Aquarium-Controller");
  ArduinoOTA.onStart([]() {
    for (int i = 0; i < RELAY_COUNT; i++) setRelayHardware(i, false);
    Serial.println("OTA update started - relays forced OFF");
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA update complete"); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA error[%u]\n", error); });
  ArduinoOTA.begin();
  Serial.println("OTA ready. Hostname: Aquarium-Controller");

  // ----------------------------------------------------------
  // Web routes
  // ----------------------------------------------------------

  server.on(
    "/",
    HTTP_GET,
    []() {

      server.send_P(
        200,
        "text/html",
        INDEX_HTML
      );

    }
  );


  server.on(
    "/api/status",
    HTTP_GET,
    handleStatus
  );


  server.on(
    "/api/relay",
    HTTP_GET,
    handleRelay
  );


  server.on(
    "/api/alloff",
    HTTP_GET,
    handleAllOff
  );

  server.on(
    "/api/resumeall",
    HTTP_GET,
    handleResumeAll
  );


  server.on(
    "/api/schedule",
    HTTP_POST,
    handleSchedule
  );


  server.on(
    "/api/device",
    HTTP_POST,
    handleDevice
  );


  server.on(
    "/api/wifi",
    HTTP_POST,
    handleWiFi
  );


  server.on(
    "/api/logs",
    HTTP_GET,
    handleLogs
  );

  server.on(
    "/api/power",
    HTTP_POST,
    handlePowerSettings
  );


  server.on(
    "/api/logs/clear",
    HTTP_GET,
    handleClearLogs
  );


  server.on(
    "/api/reset",
    HTTP_GET,
    handleReset
  );


  server.begin();


  Serial.println(
    "Web server started!"
  );


  Serial.println();

  Serial.println(
    "Open the IP address shown above in your browser."
  );

  Serial.println();

  addLog(
    "System started"
  );
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  processSchedules();
  delay(2);
}
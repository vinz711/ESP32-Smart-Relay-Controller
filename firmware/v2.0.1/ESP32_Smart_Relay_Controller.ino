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
};

RelayConfig relays[RELAY_COUNT];

// ============================================================
// WIFI DATA
// ============================================================

String wifiSSID;
String wifiPassword;

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

  uint8_t version = prefs.getUChar("schedver", 0);
  for (int i = 0; i < RELAY_COUNT; i++) {
    char key[32];
    snprintf(key, sizeof(key), "name%d", i); relays[i].name = prefs.getString(key, relays[i].name);
    snprintf(key, sizeof(key), "icon%d", i); relays[i].icon = prefs.getString(key, relays[i].icon);
    snprintf(key, sizeof(key), "auto%d", i); relays[i].autoMode = prefs.getBool(key, relays[i].autoMode);
    snprintf(key, sizeof(key), "emerg%d", i); relays[i].emergencyOff = prefs.getBool(key, false);

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
    }
  }
}

// ============================================================
// CONNECT WIFI
// ============================================================

void connectWiFi() {
  if (wifiSSID.length() == 0) {
    Serial.println("Wi-Fi SSID not configured.");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(wifiSSID);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. IP: ");
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
// HTML PAGE
// ============================================================

const char INDEX_HTML[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html lang="en">

<head>

<meta charset="UTF-8">

<meta
name="viewport" content="width=device-width, initial-scale=1.0">

<title>ESP32 Smart Relay Controller</title>

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
    0 5px 18px rgba(0,0,0,.25);
}

* {
  box-sizing:border-box;
}

body {

  margin:0;

  background:var(--bg);
  color:var(--text);

  font-family:
    Arial,
    Helvetica,
    sans-serif;
}

button,
input,
select {
  font:inherit;
}

button {
  cursor:pointer;
}

.container {

  max-width:1200px;

  margin:auto;

  padding:18px;
}

.header {

  display:flex;

  justify-content:space-between;

  align-items:center;

  gap:15px;

  margin-bottom:14px;
}

.brand {
  display:flex;
  align-items:center;
  gap:12px;
}

.brand-icon {
  font-size:42px;
}

.brand h1 {
  margin:0;
  font-size:28px;
}

.brand p {
  margin:4px 0 0;
  color:var(--muted);
  font-size:13px;
}

.top-controls {
  display:flex;
  gap:8px;
}

.theme-btn,
.settings-btn {
  border:1px solid var(--border);
  background:var(--card);
  color:var(--text);
  border-radius:9px;
  padding:9px 12px;
}

.info-bar {

  display:grid;

  grid-template-columns:
    1fr
    1fr
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
   MONITORING SUMMARY
   ========================================================= */

.monitor-grid {

  display:grid;

  grid-template-columns:
    repeat(4, 1fr);

  gap:14px;

  margin-bottom:14px;
}

.metric {

  background:var(--card);

  border:
    1px solid
    var(--border);

  border-radius:15px;

  padding:14px;

  box-shadow:var(--shadow);

  min-width:0;
}

.metric-label {

  color:var(--muted);

  font-size:12px;

  font-weight:bold;

  text-transform:uppercase;

  letter-spacing:.04em;
}

.metric-value {

  margin-top:6px;

  font-size:21px;

  font-weight:800;

  white-space:nowrap;

  overflow:hidden;

  text-overflow:ellipsis;
}

.metric-sub {

  margin-top:4px;

  color:var(--muted);

  font-size:12px;

  line-height:1.35;
}

.metric-accent {

  color:var(--blue);
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
    10px
    0;
}

.progress span {

  display:block;

  height:100%;

  background:var(--blue);

  border-radius:10px;
}

.days {

  display:flex;

  gap:5px;

  margin-bottom:10px;
}

.day {

  width:30px;
  height:30px;

  border-radius:50%;

  border:
    1px solid
    var(--border);

  background:var(--card);

  color:var(--muted);
}

.day.active {

  background:var(--blue);

  color:white;

  border-color:var(--blue);
}

.actions {

  display:grid;

  grid-template-columns:
    1fr
    1fr
    1.2fr
    1.2fr;

  gap:7px;
}

.btn {

  border:
    1px solid
    var(--border);

  background:var(--card);

  color:var(--text);

  border-radius:8px;

  padding:9px 6px;

  font-size:13px;
}

.btn-on {
  color:var(--green);
}

.btn-off {
  color:var(--red);
}

.btn-schedule {
  color:var(--blue);
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

  padding:14px;

  box-shadow:var(--shadow);
}

.panel h3 {
  margin:0 0 10px;
}

.quick {

  display:grid;

  grid-template-columns:
    1fr
    1fr
    1fr;

  gap:8px;
}

.quick button {

  border:
    1px solid
    var(--border);

  background:var(--card);

  color:var(--text);

  border-radius:8px;

  padding:10px;
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

  box-shadow:var(--shadow);
}

.log-head {

  display:flex;

  justify-content:space-between;

  align-items:center;

  padding:12px 14px;

  border-bottom:
    1px solid
    var(--border);
}

.log-head h3 {
  margin:0;
}

.clear {

  border:
    1px solid
    var(--border);

  background:var(--card);

  color:var(--red);

  border-radius:8px;

  padding:7px 10px;
}

.log-content {

  padding:12px 14px;

  max-height:230px;

  overflow:auto;

  font-family:monospace;

  font-size:12px;

  line-height:1.6;

  color:var(--muted);
}

/* =========================================================
   FOOTER
   ========================================================= */

.footer {

  display:flex;

  justify-content:space-between;

  gap:10px;

  color:var(--muted);

  font-size:12px;

  padding:15px 2px 4px;
}

/* =========================================================
   MODALS
   ========================================================= */

.modal {

  position:fixed;

  inset:0;

  background:rgba(0,0,0,.45);

  display:none;

  align-items:center;

  justify-content:center;

  padding:16px;

  z-index:50;
}

.modal.show {
  display:flex;
}

.modal-box {

  width:min(720px,100%);

  max-height:90vh;

  overflow:auto;

  background:var(--card);

  color:var(--text);

  border-radius:16px;

  padding:18px;

  box-shadow:0 20px 50px rgba(0,0,0,.25);
}

.modal-box h2 {
  margin-top:0;
}

.form-group {
  margin-bottom:12px;
}

.form-group label {
  display:block;
  margin-bottom:5px;
  font-size:13px;
  font-weight:bold;
}

.form-group input,
.form-group select {

  width:100%;

  padding:10px;

  border:
    1px solid
    var(--border);

  border-radius:8px;

  background:var(--card);

  color:var(--text);
}

.modal-actions {

  display:flex;

  justify-content:flex-end;

  gap:8px;

  margin-top:14px;
}

.modal-actions button {

  border:0;

  border-radius:8px;

  padding:10px 14px;
}

.cancel {
  background:#e6edf2;
  color:#263746;
}

.save {
  background:var(--green);
  color:white;
}

.schedule-editor {

  border:
    1px solid
    var(--border);

  border-radius:10px;

  padding:10px;

  margin-bottom:8px;
}

.schedule-editor-head {

  display:flex;

  justify-content:space-between;

  align-items:center;

  gap:8px;

  margin-bottom:8px;
}

.schedule-editor-grid {

  display:grid;

  grid-template-columns:
    1fr
    1fr;

  gap:8px;
}

.schedule-editor-days {

  display:flex;

  gap:5px;

  margin-top:8px;
}

.schedule-editor-days button {

  width:30px;
  height:30px;

  border-radius:50%;

  border:
    1px solid
    var(--border);

  background:var(--card);

  color:var(--muted);
}

.schedule-editor-days button.active {

  background:var(--blue);

  color:white;

  border-color:var(--blue);
}

@media(max-width:800px) {

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

  .monitor-grid {

    grid-template-columns:
      1fr
      1fr;

    gap:10px;

    margin-bottom:10px;
  }

  .metric {
    padding:11px;
  }

  .metric-value {
    font-size:18px;
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

  .monitor-grid {
    grid-template-columns:1fr 1fr;
  }

  .metric-value {
    font-size:17px;
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

<header class="header">

  <div class="brand">
    <div class="brand-icon">🐠</div>
    <div>
      <h1>Smart Relay Controller</h1>
      <p>ESP32 Aquarium Automation</p>
    </div>
  </div>

  <div class="top-controls">
    <button class="theme-btn" onclick="toggleTheme()">🌙 Theme</button>
    <button class="settings-btn" onclick="openNames()">⚙️ Settings</button>
  </div>

</header>

<section class="info-bar">

  <div class="info">
    <div class="info-title">🕐 IST Time</div>
    <div class="info-main" id="clock">--:--:--</div>
    <div class="info-small" id="date">--</div>
  </div>

  <div class="info">
    <div class="info-title">📶 ESP32</div>
    <div class="info-main" id="connection">Checking...</div>
    <div class="info-small" id="ip">--</div>
  </div>

  <div class="info">
    <div class="info-title">🌡️ Room Temp</div>
    <div class="info-main">Sensor not connected</div>
    <div class="info-small">Ready for future sensor</div>
  </div>

  <div class="info-actions">
    <button class="all-off" onclick="allOff()">⏻ All OFF</button>
    <button class="refresh" onclick="loadStatus()">↻ Refresh</button>
  </div>

</section>

<section class="relay-grid" id="relayGrid"></section>

<section class="monitor-grid">
  <div class="metric">
    <div class="metric-label">Active Relays</div>
    <div class="metric-value metric-accent" id="metricActive">0 / 4</div>
    <div class="metric-sub" id="metricActiveSub">All relays OFF</div>
  </div>
  <div class="metric">
    <div class="metric-label">Today's Runtime</div>
    <div class="metric-value" id="metricRuntime">0m</div>
    <div class="metric-sub" id="metricRuntimeSub">Across all relays</div>
  </div>
  <div class="metric">
    <div class="metric-label">Schedules</div>
    <div class="metric-value" id="metricSchedules">0</div>
    <div class="metric-sub" id="metricSchedulesSub">Enabled schedules</div>
  </div>
  <div class="metric">
    <div class="metric-label">Next Scheduled</div>
    <div class="metric-value" id="metricNext">--</div>
    <div class="metric-sub" id="metricNextSub">No upcoming event</div>
  </div>
</section>

<section class="lower-grid">

  <div class="panel">
    <h3>⚙️ Quick Settings</h3>
    <div class="quick">
      <button onclick="openNames()">✏️ Device Names & Icons</button>
      <button onclick="openDefaults()">🕐 Default Schedules</button>
      <button onclick="wifiSettings()">📶 Wi-Fi Settings</button>
    </div>
  </div>

  <div class="panel">
    <h3>🛡️ System Status</h3>
    <div id="systemStatus" style="color:#08b95c">● Checking system...</div>
  </div>

</section>

<section class="log">
  <div class="log-head">
    <h3>📝 Activity Log</h3>
    <button class="clear" onclick="clearLogs()">Clear</button>
  </div>
  <div class="log-content" id="logs">Loading...</div>
</section>

<footer class="footer">
  <div>🐠 Aquarium Controller v2.0.1</div>
  <div id="footerConnection">ESP32</div>
</footer>

</div>

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

<div class="modal" id="namesModal">
  <div class="modal-box">
    <h2>✏️ Device Names & Icons</h2>
    <div id="nameFields"></div>
    <div class="modal-actions">
      <button class="cancel" onclick="closeModal('namesModal')">Close</button>
    </div>
  </div>
</div>

<div class="modal" id="wifiModal">
  <div class="modal-box">
    <h2>📶 Wi-Fi Settings</h2>
    <p style="color:var(--muted);font-size:13px">Update your Wi-Fi without re-uploading the ESP32 code.</p>
    <div class="form-group">
      <label>Wi-Fi Name (SSID)</label>
      <input id="wifiSSID" autocomplete="off">
    </div>
    <div class="form-group">
      <label>Wi-Fi Password</label>
      <input type="password" id="wifiPassword" autocomplete="off">
    </div>
    <div class="modal-actions">
      <button class="cancel" onclick="closeModal('wifiModal')">Cancel</button>
      <button class="save" onclick="saveWiFi()">💾 Save Wi-Fi Settings</button>
    </div>
  </div>
</div>

<script>

let DATA = null;

const DAYS = ["M","T","W","T","F","S","S"];

let selectedDays = 127;

function $(id) {
  return document.getElementById(id);
}

function loadStatus() {

  fetch("/api/status")
  .then(response => response.json())
  .then(data => {
    DATA = data;
    render();
  })
  .catch(() => {
    $("connection").textContent = "Disconnected";
    $("systemStatus").innerHTML = "<span style='color:#f23845'>● ESP32 disconnected</span>";
  });

  fetch("/api/logs")
  .then(response => response.json())
  .then(data => {
    if (!data.logs.length) {
      $("logs").innerHTML = "No activity yet.";
      return;
    }
    $("logs").innerHTML = data.logs.slice().reverse().map(x => escapeHtml(x)).join("<br>");
  });
}

function render() {

  if (!DATA) return;

  $("connection").textContent = DATA.wifi ? "Connected" : "Disconnected";
  $("ip").textContent = DATA.ip;
  $("footerConnection").textContent = "ESP32 " + (DATA.wifi ? "Connected" : "Disconnected") + " | " + DATA.ip;

  let anyEmergency = DATA.relays.some(r => r.emergency);
  $("systemStatus").innerHTML = DATA.wifi
    ? (anyEmergency
        ? "<b style='color:#f23845'>● Emergency OFF active</b><br>One or more relays are isolated from schedules. <button onclick=\"resumeAll()\">Resume AUTO</button>"
        : "<b style='color:#08b95c'>● All systems normal</b><br>4 devices configured • Wi-Fi connected • Schedules active")
    : "<b style='color:#f23845'>● Wi-Fi disconnected</b>";

  let html = "";

  DATA.relays.forEach((relay, index) => {

    let runtime = formatRuntime(relay.runtime);
    let progress = Math.min(100, (relay.runtime / 21600000) * 100);
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
          <div class="device-icon">${escapeHtml(relay.icon)}</div>
          <div class="device-title">
            <h2>${escapeHtml(relay.name)}</h2>
            <p>Relay ${relay.id} • ${relay.mode} mode</p>
          </div>
          <div class="switch ${relay.state ? "on" : ""}" onclick="toggleRelay(${relay.id},${relay.state ? "off" : "on"})"></div>
        </div>
        <div class="status ${relay.state ? "on" : "off"}">${relay.state ? "● ON" : "● OFF"} • Today ${runtime}</div>
        <div class="runtime">
          <span class="schedule-text">${scheduleText}</span>
          <span class="next-action">${nextText}</span>
        </div>
        <div class="progress"><span style="width:${progress}%"></span></div>
        <div class="days">${daysHTML}</div>
        <div class="actions">
          <button class="btn btn-on" onclick="toggleRelay(${relay.id},'on')">▶ On</button>
          <button class="btn btn-off" onclick="toggleRelay(${relay.id},'off')">■ Off</button>
          <button class="btn btn-schedule" onclick="openSchedule(${relay.id})">📅 Schedule</button>
          ${relay.emergency ? `<button class="btn btn-off" onclick="resumeRelay(${relay.id})">▶ Resume AUTO</button>` : `<button class="btn" style="border:1px solid #ff8a8a" onclick="emergencyOff(${relay.id})">🚨 Emergency OFF</button>`}
        </div>
      </article>`;
  });

  $("relayGrid").innerHTML = html;
  updateMonitoringSummary();
}

function updateMonitoringSummary() {

  if (!DATA || !DATA.relays) return;

  let active = DATA.relays.filter(r => r.state).length;
  let totalRuntime = DATA.relays.reduce((sum, r) => sum + (Number(r.runtime) || 0), 0);
  let enabledSchedules = DATA.relays.reduce((sum, r) => sum + r.schedules.filter(s => s.enabled).length, 0);

  $("metricActive").textContent = active + " / " + DATA.relays.length;
  $("metricActiveSub").textContent = active === 0 ? "All relays OFF" : active === 1 ? "1 relay currently ON" : active + " relays currently ON";
  $("metricRuntime").textContent = formatRuntime(totalRuntime);
  $("metricRuntimeSub").textContent = "Combined runtime today";
  $("metricSchedules").textContent = enabledSchedules;
  $("metricSchedulesSub").textContent = enabledSchedules === 1 ? "1 enabled schedule" : enabledSchedules + " enabled schedules";

  let next = getNextScheduledEvent();
  $("metricNext").textContent = next ? next.time : "--";
  $("metricNextSub").textContent = next ? next.label : "No upcoming schedule";
}

function getNextScheduledEvent() {

  if (!DATA || !DATA.relays) return null;

  let now = new Date();
  let currentDay = (now.getDay() + 6) % 7;
  let currentMinutes = now.getHours() * 60 + now.getMinutes();
  let candidates = [];

  DATA.relays.forEach(relay => {
    if (relay.mode !== "AUTO" || relay.emergency) return;

    relay.schedules.forEach(slot => {
      if (!slot.enabled || slot.start === slot.stop) return;

      for (let offset = 0; offset < 7; offset++) {
        let day = (currentDay + offset) % 7;
        if (!(slot.days & (1 << day))) continue;

        let minutes = slot.start;
        if (offset === 0 && minutes <= currentMinutes) continue;

        candidates.push({offset: offset, minutes: minutes, relay: relay.name});
      }
    });
  });

  if (!candidates.length) return null;

  candidates.sort((a, b) => {
    let aKey = a.offset * 1440 + a.minutes;
    let bKey = b.offset * 1440 + b.minutes;
    return aKey - bKey;
  });

  let next = candidates[0];
  return {time: minutesToTime(next.minutes), label: next.relay + " • " + DAYS[next.offset]};
}

function formatRuntime(ms) {
  let minutes = Math.floor(ms / 60000);
  let hours = Math.floor(minutes / 60);
  minutes = minutes % 60;
  if (hours > 0) return hours + "h " + minutes + "m";
  return minutes + "m";
}

function minutesToTime(minutes) {
  let hour = Math.floor(minutes / 60);
  let minute = minutes % 60;
  let suffix = hour >= 12 ? "PM" : "AM";
  let displayHour = hour % 12 || 12;
  return String(displayHour).padStart(2,"0") + ":" + String(minute).padStart(2,"0") + " " + suffix;
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g,"&amp;")
    .replace(/</g,"&lt;")
    .replace(/>/g,"&gt;")
    .replace(/\"/g,"&quot;")
    .replace(/'/g,"&#039;");
}

function toggleRelay(id, action) {
  fetch("/api/relay", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:"id=" + id + "&action=" + action})
    .then(async r => { if (!r.ok) throw new Error(await r.text()); return r.text(); })
    .then(loadStatus)
    .catch(e => alert(e.message || "Unable to change relay state."));
}

function allOff() {
  if (!confirm("Turn all relays OFF?")) return;
  fetch("/api/alloff", {method:"POST"}).then(loadStatus).catch(() => alert("Unable to turn relays off."));
}

function emergencyOff(id) {
  if (!confirm("Emergency OFF will force this relay OFF and ignore all schedules until you resume AUTO. Continue?")) return;
  let params = new URLSearchParams(); params.append("id", id);
  fetch("/api/emergency", {method:"POST", body:params}).then(loadStatus).catch(e => alert(e.message || "Unable to activate Emergency OFF."));
}

function resumeRelay(id) {
  let params = new URLSearchParams(); params.append("id", id);
  fetch("/api/resume", {method:"POST", body:params}).then(loadStatus).catch(e => alert(e.message || "Unable to resume relay."));
}

function resumeAll() {
  fetch("/api/resumeall", {method:"POST"}).then(loadStatus).catch(e => alert(e.message || "Unable to resume AUTO."));
}

function openSchedule(id) {
  if (!DATA) return;
  let relay = DATA.relays.find(r => r.id === id);
  if (!relay) return;
  $("scheduleId").value = id;
  $("scheduleTitle").textContent = "📅 " + relay.name + " Schedules";
  $("scheduleMode").value = relay.mode;

  let html = "";
  relay.schedules.forEach((slot, i) => {
    let dayButtons = "";
    for (let d = 0; d < 7; d++) dayButtons += `<button type="button" class="${slot.days & (1 << d) ? "active" : ""}" onclick="toggleScheduleDay(${i},${d})">${DAYS[d]}</button>`;
    html += `<div class="schedule-editor">
      <div class="schedule-editor-head"><b>Schedule ${i + 1}</b><label><input type="checkbox" id="schedEnabled${i}" ${slot.enabled ? "checked" : ""}> Enabled</label></div>
      <div class="schedule-editor-grid"><div><label>Start</label><input type="time" id="schedStart${i}" value="${timeInput(slot.start)}"></div><div><label>Stop</label><input type="time" id="schedStop${i}" value="${timeInput(slot.stop)}"></div></div>
      <div class="schedule-editor-days" id="schedDays${i}">${dayButtons}</div>
    </div>`;
  });
  $("scheduleList").innerHTML = html;
  $("scheduleModal").classList.add("show");
}

function toggleScheduleDay(slotId, day) {
  let buttons = $("schedDays" + slotId).querySelectorAll("button");
  if (!buttons[day]) return;
  buttons[day].classList.toggle("active");
}

function timeInput(minutes) {
  let h = Math.floor(minutes / 60); let m = minutes % 60;
  return String(h).padStart(2,"0") + ":" + String(m).padStart(2,"0");
}

function closeModal(id) { $(id).classList.remove("show"); }

function openNames() {
  if (!DATA) return;
  let html = "";
  DATA.relays.forEach(r => {
    html += `<div class="form-group"><label>Relay ${r.id} Name</label><input id="name${r.id}" value="${escapeHtml(r.name)}"><label>Icon</label><input id="icon${r.id}" value="${escapeHtml(r.icon)}"></div>`;
  });
  $("nameFields").innerHTML = html;
  $("namesModal").classList.add("show");
}

function wifiSettings() {
  $("wifiSSID").value = "";
  $("wifiPassword").value = "";
  $("wifiModal").classList.add("show");
}

function toggleTheme() {
  document.body.classList.toggle("dark");
  localStorage.setItem("theme", document.body.classList.contains("dark") ? "dark" : "light");
}

function loadTheme() {
  if (localStorage.getItem("theme") === "dark") document.body.classList.add("dark");
}

function clearLogs() {
  if (!confirm("Clear activity log?")) return;
  fetch("/api/logs/clear", {method:"POST"}).then(loadStatus).catch(() => alert("Unable to clear logs."));
}

function tickClock() {
  let now = new Date();
  $("clock").textContent = now.toLocaleTimeString();
  $("date").textContent = now.toLocaleDateString(undefined, {weekday:"short", year:"numeric", month:"short", day:"numeric"});
}

function saveWiFi() {
  let ssid = $("wifiSSID").value.trim();
  let password = $("wifiPassword").value;
  if (!ssid) { alert("SSID cannot be empty."); return; }
  let params = new URLSearchParams(); params.append("ssid", ssid); params.append("password", password);
  fetch("/api/wifi", {method:"POST", body:params}).then(async r => { if (!r.ok) throw new Error(await r.text()); return r.text(); }).then(() => { closeModal("wifiModal"); alert("Wi-Fi settings saved. The ESP32 will reconnect using the new settings."); loadStatus(); }).catch(e => alert(e.message || "Unable to save Wi-Fi settings."));
}

function openDefaults() {
  if (!DATA) return;
  alert("Each relay supports up to 6 AUTO schedules. Use the Schedule button to configure them. MANUAL mode keeps direct control, while Emergency OFF overrides all schedules.");
}

function toggleDay(id, day) {
  let relay = DATA.relays.find(r => r.id === id);
  if (!relay || !relay.schedules.length) return;
  let slot = relay.schedules[0];
  slot.days ^= (1 << day);
  let params = new URLSearchParams();
  params.append("id", id); params.append("slot", 0); params.append("enabled", slot.enabled ? "1" : "0"); params.append("start", slot.start); params.append("stop", slot.stop); params.append("days", slot.days); params.append("mode", relay.mode);
  fetch("/api/schedule", {method:"POST", body:params}).then(loadStatus).catch(e => alert(e.message || "Unable to update schedule."));
}

function saveSchedule() {
  let id = Number($("scheduleId").value);
  let relay = DATA.relays.find(r => r.id === id);
  if (!relay) return;
  let requests = [];
  for (let i = 0; i < relay.schedules.length; i++) {
    let start = $("schedStart" + i); let stop = $("schedStop" + i); let enabled = $("schedEnabled" + i);
    if (!start || !stop || !enabled) continue;
    let days = 0; let buttons = $("schedDays" + i).querySelectorAll("button");
    buttons.forEach((b,d) => { if (b.classList.contains("active")) days |= (1 << d); });
    let params = new URLSearchParams(); params.append("id", id); params.append("slot", i); params.append("enabled", enabled.checked ? "1" : "0"); params.append("start", timeToMinutes(start.value)); params.append("stop", timeToMinutes(stop.value)); params.append("days", days); params.append("mode", $("scheduleMode").value);
    requests.push(fetch("/api/schedule", {method:"POST", body:params}).then(async r => { if (!r.ok) throw new Error(await r.text()); }));
  }
  Promise.all(requests).then(() => { closeModal("scheduleModal"); loadStatus(); }).catch(e => alert(e.message || "Unable to save schedules."));
}

function timeToMinutes(value) { let parts = value.split(":"); return Number(parts[0]) * 60 + Number(parts[1]); }

loadTheme();
tickClock();
setInterval(tickClock,1000);
loadStatus();
setInterval(loadStatus,10000);

</script>

</body>
</html>

)rawliteral";

// ============================================================
// API HANDLERS
// ============================================================

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  server.send(200, "application/json", buildStatusJSON());
}

void handleLogs() {
  String json = "{\"logs\":[";
  for (int i = 0; i < logCount; i++) {
    if (i > 0) json += ",";
    json += "\"" + jsonEscape(activityLogs[i]) + "\"";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleClearLogs() {
  logCount = 0;
  server.send(200, "text/plain", "OK");
}

void handleRelay() {
  if (!server.hasArg("id") || !server.hasArg("action")) {
    server.send(400, "text/plain", "Missing id/action");
    return;
  }

  int id = server.arg("id").toInt() - 1;
  String action = server.arg("action");

  if (id < 0 || id >= RELAY_COUNT) {
    server.send(400, "text/plain", "Invalid relay");
    return;
  }

  RelayConfig &r = relays[id];

  if (r.emergencyOff) {
    server.send(409, "text/plain", "Emergency OFF is active. Resume AUTO first.");
    return;
  }

  bool on = action == "on";
  bool scheduledNow = r.autoMode && isAnyScheduleActive(id);

  if (!on && r.autoMode && scheduledNow) {
    server.send(409, "text/plain", "An AUTO schedule is active. Use Emergency OFF to force OFF.");
    return;
  }

  r.manualOverride = r.autoMode && !scheduledNow;
  setRelayState(id, on, "Manual");
  server.send(200, "text/plain", "OK");
}

void handleAllOff() {
  for (int i = 0; i < RELAY_COUNT; i++) {
    relays[i].manualOverride = true;
    if (!relays[i].emergencyOff) setRelayState(i, false, "All OFF");
  }
  server.send(200, "text/plain", "OK");
}

void handleEmergency() {
  if (!server.hasArg("id")) { server.send(400, "text/plain", "Missing id"); return; }
  int id = server.arg("id").toInt() - 1;
  if (id < 0 || id >= RELAY_COUNT) { server.send(400, "text/plain", "Invalid relay"); return; }
  relays[id].emergencyOff = true;
  relays[id].manualOverride = false;
  setRelayState(id, false, "Emergency OFF");
  saveRelaySettings(id);
  server.send(200, "text/plain", "OK");
}

void handleResume() {
  if (!server.hasArg("id")) { server.send(400, "text/plain", "Missing id"); return; }
  int id = server.arg("id").toInt() - 1;
  if (id < 0 || id >= RELAY_COUNT) { server.send(400, "text/plain", "Invalid relay"); return; }
  relays[id].emergencyOff = false;
  relays[id].manualOverride = false;
  saveRelaySettings(id);
  setRelayState(id, isAnyScheduleActive(id), "Resume AUTO");
  server.send(200, "text/plain", "OK");
}

void handleResumeAll() {
  for (int i = 0; i < RELAY_COUNT; i++) {
    relays[i].emergencyOff = false;
    relays[i].manualOverride = false;
    saveRelaySettings(i);
    setRelayState(i, isAnyScheduleActive(i), "Resume AUTO");
  }
  server.send(200, "text/plain", "OK");
}

void handleSchedule() {
  if (!server.hasArg("id") || !server.hasArg("slot") || !server.hasArg("enabled") || !server.hasArg("start") || !server.hasArg("stop") || !server.hasArg("days") || !server.hasArg("mode")) {
    server.send(400, "text/plain", "Missing schedule arguments"); return;
  }

  int id = server.arg("id").toInt() - 1;
  int slotId = server.arg("slot").toInt();
  if (id < 0 || id >= RELAY_COUNT || slotId < 0 || slotId >= MAX_SCHEDULES) {
    server.send(400, "text/plain", "Invalid relay or schedule slot"); return;
  }

  RelayConfig &r = relays[id];
  ScheduleSlot &slot = r.schedules[slotId];
  slot.enabled = server.arg("enabled").toInt() != 0;
  slot.startMinutes = server.arg("start").toInt();
  slot.stopMinutes = server.arg("stop").toInt();
  slot.days = server.arg("days").toInt();
  r.autoMode = server.arg("mode") == "AUTO";
  r.manualOverride = false;

  if (slot.enabled && (slot.startMinutes == slot.stopMinutes || slot.days == 0)) {
    server.send(400, "text/plain", "Enabled schedule needs different start/stop times and at least one day"); return;
  }

  saveRelaySettings(id);
  addLog(r.name + " schedule " + String(slotId + 1) + " updated");
  server.send(200, "text/plain", "OK");
}

void handleNames() {
  for (int i = 0; i < RELAY_COUNT; i++) {
    String nameKey = "name" + String(i + 1);
    String iconKey = "icon" + String(i + 1);
    if (server.hasArg(nameKey)) relays[i].name = urlDecode(server.arg(nameKey));
    if (server.hasArg(iconKey)) relays[i].icon = urlDecode(server.arg(iconKey));
    saveRelaySettings(i);
  }
  server.send(200, "text/plain", "OK");
}

void handleWiFi() {
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    server.send(400, "text/plain", "Missing Wi-Fi settings"); return;
  }

  String newSSID = urlDecode(server.arg("ssid"));
  String newPassword = server.arg("password");

  if (newSSID.length() == 0) {
    server.send(400, "text/plain", "SSID cannot be empty"); return;
  }

  prefs.putString("ssid", newSSID);
  prefs.putString("pass", newPassword);

  wifiSSID = newSSID;
  wifiPassword = newPassword;

  server.send(200, "text/plain", "Wi-Fi settings saved. Reconnecting...");

  delay(300);
  WiFi.disconnect(true);
  delay(300);
  connectWiFi();
}

// ============================================================
// OTA
// ============================================================

void setupOTA() {
  ArduinoOTA.setHostname("ESP32-Smart-Relay-Controller");

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("OTA End");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]\n", error);
  });

  ArduinoOTA.begin();
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  for (int i = 0; i < RELAY_COUNT; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], RELAY_OFF);
  }

  loadSettings();
  connectWiFi();
  setupOTA();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/logs", HTTP_GET, handleLogs);
  server.on("/api/logs/clear", HTTP_POST, handleClearLogs);
  server.on("/api/relay", HTTP_POST, handleRelay);
  server.on("/api/alloff", HTTP_POST, handleAllOff);
  server.on("/api/emergency", HTTP_POST, handleEmergency);
  server.on("/api/resume", HTTP_POST, handleResume);
  server.on("/api/resumeall", HTTP_POST, handleResumeAll);
  server.on("/api/schedule", HTTP_POST, handleSchedule);
  server.on("/api/names", HTTP_POST, handleNames);
  server.on("/api/wifi", HTTP_POST, handleWiFi);

  server.begin();
  addLog("System started");
  Serial.println("Web server started.");
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  processSchedules();
}

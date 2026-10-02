/*
  AQUARIUM CONTROLLER - v3.2.8
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

#define FW_VERSION "3.2.8"
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
// SAME mapping as the tested v2.0.1 board.
const uint8_t relayPins[RELAY_COUNT] = {19, 18, 5, 17};

// Active LOW relay board
const uint8_t RELAY_ON = LOW;
const uint8_t RELAY_OFF = HIGH;

// ---------------- DEFAULT DEVICE DATA ----------------
const char* DEFAULT_NAMES[RELAY_COUNT] = {
  "Main Tank Filter",
  "Aquarium Lights",
  "Wave Maker",
  "Cooler Unit"
};

const char* DEFAULT_ICONS[RELAY_COUNT] = {
  "🐟", "💡", "🌊", "❄️"
};

// Software-only estimated wattages; change them from the UI.
const uint16_t DEFAULT_WATTS[RELAY_COUNT] = {0, 0, 0, 0}; // 0 = not configured; enter actual wattage manually

// ---------------- SERVER / STORAGE ----------------
WebServer server(80);
Preferences prefs;

// ---------------- DATA ----------------
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
  bool emergencySavedState;
  bool emergencySavedManualOverride;
  uint16_t watts;
  ScheduleSlot schedules[MAX_SCHEDULES];

  uint64_t runtimeTodayMs;
  uint64_t runtimeTotalMs;
  unsigned long stateStartedMillis;
};

RelayConfig relays[RELAY_COUNT];

String wifiSSID;
String wifiPassword;
String activityLogs[MAX_LOGS];
uint16_t logCount = 0;

int lastWeekday = -1;
unsigned long lastScheduleCheck = 0;
unsigned long lastWiFiCheck = 0;
unsigned long bootMillis = 0;
bool fallbackAP = false;
unsigned long lastRuntimeSave = 0;
bool runtimeDirty = false;
const unsigned long RUNTIME_SAVE_INTERVAL_MS = 30000UL;
const char* LOG_FILE = "/activity.log";

// Cached clock: never block an HTTP request waiting for NTP/time sync.
struct tm cachedTimeInfo;
bool cachedTimeValid = false;
unsigned long lastClockUpdate = 0;
unsigned long lastBrowserTimeSync = 0;
bool schedulerTimeReady = false;

// ============================================================
// NEW SMART MANAGEMENT UI
// ============================================================
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Aquarium Control</title>
<style>
:root{
 --bg:#f3f8fc;--card:#ffffff;--text:#10234b;--muted:#64748b;--border:#dce7f0;
 --green:#08a96a;--green2:#0bb878;--red:#f43f4e;--blue:#1477d4;--teal:#087f72;
 --purple:#6436c7;--yellow:#f5bd14;--shadow:0 5px 18px rgba(24,55,90,.09)
}
body.dark{--bg:#091622;--card:#102432;--text:#edf6ff;--muted:#a7b8c6;--border:#254353;--shadow:0 5px 18px rgba(0,0,0,.28)}
*{box-sizing:border-box}
body{margin:0;background:linear-gradient(180deg,#f7fbfe 0%,var(--bg) 100%);color:var(--text);font-family:Arial,Helvetica,sans-serif}
button,input,select{font:inherit}button{cursor:pointer}
.container{max-width:1440px;margin:auto;padding:20px 18px 14px}
.header{display:flex;justify-content:space-between;align-items:center;gap:16px;margin-bottom:16px}
.brand{display:flex;align-items:center;gap:13px}.brand-icon{font-size:48px;line-height:1}
.brand h1{margin:0;font-size:32px;letter-spacing:-.6px}.brand p{margin:3px 0 0;color:#53657d;font-size:18px}
.top-actions{display:flex;gap:10px;align-items:center}.top-btn{border:1px solid var(--border);background:var(--card);color:var(--text);padding:11px 15px;border-radius:10px;font-weight:600;box-shadow:0 1px 4px rgba(0,0,0,.03)}
.power-btn{background:#087f72;color:#fff;border:0;font-weight:bold}.alloff{border:1px solid #ff7180!important;color:#ef3340!important;background:#fff!important;font-weight:700}
.summary{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin-bottom:18px}
.summary-card{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:16px 18px;box-shadow:var(--shadow);min-height:96px}
.summary-label{font-size:14px;color:#52667d;font-weight:600}.summary-value{font-size:27px;font-weight:800;margin-top:8px;line-height:1.05}.summary-sub{font-size:14px;color:var(--muted);margin-top:5px}
.section-title{font-size:23px;font-weight:800;margin:12px 0 10px;letter-spacing:-.2px}
.relay-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.relay{background:var(--card);border:1px solid var(--border);border-left:5px solid #18a8b9;border-radius:14px;padding:15px;box-shadow:var(--shadow);min-height:230px}
.relay:nth-child(1){border-left-color:#08b95c}.relay:nth-child(2){border-left-color:#159bea}.relay:nth-child(3){border-left-color:#f0bd00}.relay:nth-child(4){border-left-color:#8b4ce8}
.relay-head{display:flex;align-items:center;gap:11px}.icon{width:46px;height:46px;border-radius:11px;background:#edf6fb;display:flex;align-items:center;justify-content:center;font-size:28px;flex:none}
body.dark .icon{background:#173742}.title{flex:1;min-width:0}.title h2{margin:0;font-size:18px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.title p{margin:3px 0 0;color:var(--muted);font-size:11px}
.badge{display:inline-block;background:#edf4fb;color:#334155;padding:7px 9px;border-radius:7px;font-size:12px;margin-left:4px}.mode-badge{display:inline-block;padding:5px 9px;border-radius:7px;font-weight:700;font-size:12px;margin-left:7px}.auto-badge{background:#dff7eb;color:#0c874d}.manual-badge{background:#e2efff;color:#1768b5}
.switch{width:54px;height:30px;border-radius:30px;background:#7a8790;position:relative;flex:none}.switch:after{content:"";position:absolute;width:22px;height:22px;top:4px;left:4px;border-radius:50%;background:#fff;transition:.2s}.switch.on{background:var(--green)}.switch.on:after{left:28px}
.status{font-size:12px;margin-top:9px;font-weight:700}.onText{color:#0a9657}.offText{color:#ef3340}
.meta{margin-top:8px;background:#f0f6fb;border:1px solid var(--border);border-radius:7px;padding:9px;font-size:12px;line-height:1.45}.meta b{font-size:12px}body.dark .meta{background:#132f3a}
.next-action{font-size:13px;color:#1477d4;margin:8px 0 10px;font-weight:600}.next-action span{font-weight:400}
.days{display:flex;gap:5px;margin:6px 0 9px;flex-wrap:wrap}.day{border:0;width:22px;height:22px;border-radius:50%;background:#08b765;color:#fff;font-size:9px;font-weight:bold;padding:0}.day:not(.active){background:#d9e6ed;color:#456174}
.actions{display:grid;grid-template-columns:1fr 1fr 1.05fr 1.05fr;gap:8px}.btn{border:0;border-radius:8px;padding:10px 6px;font-weight:700;font-size:13px}.onBtn{background:var(--green);color:#fff}.offBtn{background:var(--red);color:#fff}.outline{background:var(--card);color:var(--text);border:1px solid var(--border)}
.emergency{margin-top:7px;width:100%;background:transparent;color:var(--red);border:1px solid #ff8b95}.resume{margin-top:7px;width:100%;background:#0d7168;color:#fff;border:0}
.power-section{margin-top:14px;background:var(--card);border:1px solid var(--border);border-radius:14px;padding:15px;box-shadow:var(--shadow)}
.power-title{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:10px}.power-title h3{margin:0;font-size:22px}.info-note{background:#eaf4ff;border:1px solid #c7dff7;color:#1b67a7;padding:7px 10px;border-radius:7px;font-size:12px}
.power-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}.power-card{border:1px solid var(--border);border-radius:10px;padding:12px;background:linear-gradient(180deg,#fff 0%,#fbfdff 100%)}
.power-head{display:flex;justify-content:space-between;gap:8px;font-weight:700;font-size:14px;align-items:center}.power-watt-badge{background:#edf4fb;padding:6px 8px;border-radius:6px;white-space:nowrap;font-size:12px}.power-card label{display:block;font-size:11px;color:var(--muted);margin:10px 0 4px}.power-card input[type=number]{width:100%;padding:9px;border:1px solid var(--border);border-radius:7px;background:var(--card);color:var(--text)}
.power-card input[type=range]{width:100%;accent-color:#0b8b78;margin:10px 0 2px}.range-labels{display:flex;justify-content:space-between;color:var(--muted);font-size:10px}.power-actions{display:flex;gap:7px;margin-top:9px}.power-actions button{flex:1;padding:9px;border-radius:7px;border:0;background:#087f72;color:#fff;font-weight:600}.power-actions .reset{background:var(--card);color:var(--text);border:1px solid var(--border)}
.cost{margin-top:7px;font-size:10px;color:var(--muted)}
.lower{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-top:14px}.panel{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:15px;box-shadow:var(--shadow)}.panel h3{margin:0 0 10px;font-size:18px}.quick{display:flex;gap:8px;flex-wrap:wrap}.quick button{border:1px solid var(--border);background:var(--card);color:var(--text);padding:9px 11px;border-radius:8px;font-size:12px}
.status-panel{display:grid;grid-template-columns:1.2fr 1fr 1fr 1fr;gap:10px;align-items:center}.status-main{font-weight:700;color:#0b9856}.status-stat{font-size:12px;color:var(--muted)}.status-stat b{display:block;color:var(--text);font-size:13px;margin-top:2px}
.log{margin-top:14px;background:var(--card);border:1px solid var(--border);border-radius:14px;padding:15px;box-shadow:var(--shadow)}.loghead{display:flex;justify-content:space-between;align-items:center;gap:10px}.loghead h3{margin:0;font-size:21px}.logtools{display:flex;gap:7px;align-items:center;flex-wrap:wrap}.logtools input{padding:8px;border:1px solid var(--border);border-radius:7px;background:var(--card);color:var(--text)}
.logs{max-height:245px;overflow:auto;margin-top:10px;border:1px solid var(--border);border-radius:8px}.logtable{width:100%;border-collapse:collapse;font-size:12px}.logtable th{position:sticky;top:0;background:#f2f6fa;color:#22344e;text-align:left;padding:8px;border-bottom:1px solid var(--border)}.logtable td{padding:7px 8px;border-bottom:1px solid #edf1f5;color:#56657a}.logtable .green{color:#07884d;font-weight:700}.logtable .red{color:#e3293d;font-weight:700}.logtable .blue{color:#1469bd;font-weight:600}body.dark .logtable th{background:#17303c}body.dark .logtable td{border-color:#223c49}
.footer{display:flex;justify-content:space-between;color:var(--muted);font-size:11px;margin-top:10px}
.modal{display:none;position:fixed;inset:0;background:rgba(0,0,0,.58);z-index:20;align-items:center;justify-content:center;padding:12px}.modal.show{display:flex}.box{width:min(650px,100%);max-height:92vh;overflow:auto;background:var(--card);color:var(--text);border-radius:14px;padding:18px}.box h2{margin-top:0}.field{margin:10px 0}.field label{display:block;font-size:12px;color:var(--muted);margin-bottom:4px}.field input,.field select{width:100%;padding:9px;border:1px solid var(--border);border-radius:7px;background:var(--card);color:var(--text)}.modal-actions{display:flex;gap:8px;margin-top:12px}.modal-actions button{flex:1;padding:10px;border:0;border-radius:8px}.save{background:#147fb7;color:#fff}.cancel{background:#dfe8ed;color:#19354a}.schedule-card{border:1px solid var(--border);border-radius:10px;padding:10px;margin:9px 0}.schedule-top{display:flex;justify-content:space-between;align-items:center}.week{display:flex;gap:5px;flex-wrap:wrap;margin-top:7px}.week button{width:28px;height:28px;border:0;border-radius:50%;background:#dce7ed;color:#385568;font-size:10px;font-weight:bold}.week button.active{background:var(--green);color:#fff}.notice{background:#fff7e2;border:1px solid #eed17a;padding:9px;border-radius:8px;font-size:12px;margin:8px 0}body.dark .notice{background:#3b3219;border-color:#665721}
@media(max-width:1000px){.power-grid{grid-template-columns:1fr 1fr}.status-panel{grid-template-columns:1fr 1fr}}
@media(max-width:760px){.container{padding:10px}.header{align-items:flex-start}.brand h1{font-size:24px}.brand p{font-size:13px}.brand-icon{font-size:38px}.top-actions{flex-wrap:wrap;justify-content:flex-end}.summary{grid-template-columns:1fr 1fr;gap:8px}.summary-value{font-size:21px}.relay-grid,.lower{grid-template-columns:1fr}.power-grid{grid-template-columns:1fr}.actions{grid-template-columns:1fr 1fr}.status-panel{grid-template-columns:1fr 1fr}.footer{flex-direction:column;gap:4px}.loghead{align-items:flex-start;flex-direction:column}.logs{font-size:11px}.logtable{min-width:680px}}
</style>
</head>
<body>
<div class="container">
<header class="header">
  <div class="brand"><div class="brand-icon">🐠</div><div><h1>Aquarium Control</h1><p>Smart Management System</p></div></div>
  <div class="top-actions">
    <button class="top-btn" onclick="toggleTheme()">🌙</button>
    <button class="top-btn power-btn" onclick="scrollPower()">⚡ Power Settings</button>
    <button class="top-btn alloff" onclick="allOff()">⏻<br>ALL OFF</button>
  </div>
</header>

<section class="summary">
  <div class="summary-card"><div class="summary-label">⏱ Active Relays</div><div class="summary-value" id="activeRelays">0 / 4</div></div>
  <div class="summary-card"><div class="summary-label">◷ Today's Runtime</div><div class="summary-value" id="totalRuntime">0m</div></div>
  <div class="summary-card"><div class="summary-label">⚡ Est. Power Usage</div><div class="summary-value" id="energy">0.000 kWh</div><div class="summary-sub" id="energyDetail">0.000 Wh • 0 W now</div></div>
  <div class="summary-card"><div class="summary-label">🎯 Next Scheduled</div><div class="summary-value" id="nextSchedule">--</div></div>
</section>

<div class="section-title">🔌 Relay Control</div>
<section class="relay-grid" id="relayGrid"></section>

<section class="power-section" id="powerSection">
  <div class="power-title"><h3>⚡ Power Settings</h3><div class="info-note">ⓘ Edit power ratings for each device. These are used to calculate estimated power usage.</div></div>
  <div class="power-grid" id="powerGrid"></div>
  <div class="notice">Enter the actual/rated power consumption of each device. These values are used only to estimate energy usage (kWh); this controller has no electrical power sensor.</div>
</section>

<section class="lower">
  <div class="panel"><h3>⚙️ Quick Settings</h3><div class="quick">
    <button onclick="openNames()">✏️ Device Names & Icons</button>
    <button onclick="openDefaults()">🕐 Default Schedules</button>
    <button onclick="wifiSettings()">📶 Wi-Fi Settings</button>
     <button onclick="otaInfo()">⬆️ OTA Update</button>
    <button onclick="downloadBackup()">⬇️ Backup</button>
    <button onclick="restoreInput.click()">⬆️ Restore</button>
    <button onclick="resetController()">♻️ Factory Reset</button>
    <input id="restoreInput" type="file" accept=".json,application/json" style="display:none">
  </div></div>
  <div class="panel"><h3>🛡️ System Status</h3><div class="status-panel">
    <div id="systemStatus" class="status-main">Checking...</div>
    <div class="status-stat">IP Address<b id="statusIP">--</b></div>
    <div class="status-stat">RSSI<b id="statusRSSI">--</b></div>
    <div class="status-stat">Uptime<b id="statusUptime">--</b></div>
  </div><div style="margin-top:8px;font-size:12px;color:var(--muted)" id="systemInfo"></div><div style="margin-top:5px;font-size:11px;color:var(--muted)" id="networkInfo">Network status: checking...</div></div>
</section>

<section class="log"><div class="loghead"><h3>📝 Activity Log <span id="logCountLabel" style="font-size:11px;color:var(--muted)">(latest 300)</span></h3><div class="logtools"><input id="logSearch" placeholder="Search logs..." style="padding:8px;border:1px solid var(--border);border-radius:7px;background:var(--card);color:var(--text)" oninput="filterLogs()"><button class="top-btn" onclick="exportLogs()">⬇️ Export</button><button class="top-btn" style="color:#f23d4b" onclick="clearLogs()">🗑 Clear</button></div></div>
  <div class="logs" id="logs">Loading...</div>
</section>

<div class="footer"><div>🐠 Aquarium Controller v3.2.8 • Persistent logs: 300 events</div><div id="footerConnection">ESP32</div></div>
</div>

<div class="modal" id="scheduleModal"><div class="box">
  <h2 id="scheduleTitle">📅 Schedules</h2><div class="field"><label>Relay Mode</label>
  <select id="scheduleMode"><option value="AUTO">AUTO</option><option value="MANUAL">MANUAL</option></select></div>
  <div id="scheduleList"></div><div class="modal-actions"><button class="cancel" onclick="closeModal('scheduleModal')">Close</button></div>
</div></div>

<div class="modal" id="namesModal"><div class="box"><h2>✏️ Device Names & Icons</h2><div id="nameFields"></div>
  <div class="modal-actions"><button class="cancel" onclick="closeModal('namesModal')">Close</button></div>
</div></div>

<div class="modal" id="wifiModal"><div class="box"><h2>📶 Wi-Fi Settings</h2>
  <div class="notice">The ESP32 will restart after saving. Schedules and relay settings remain stored.</div>
  <div class="field"><label>Wi-Fi SSID</label><input id="wifiSSID" autocomplete="off"></div>
  <div class="field"><label>Wi-Fi Password</label><input id="wifiPassword" type="password" autocomplete="off"></div>
  <div class="modal-actions"><button class="cancel" onclick="closeModal('wifiModal')">Cancel</button><button class="save" onclick="saveWiFi()">Save Wi-Fi</button></div>
</div></div>

<script>
let DATA=null;let LOGS=[];const DAYS=["M","T","W","T","F","S","S"];
function $(id){return document.getElementById(id)}
function api(url,opt={}){return fetch(url,opt).then(async r=>{if(!r.ok)throw new Error(await r.text());return r})}
function demoStatus(){
  const names=["Main Tank Filter","Aquarium Lights","Wave Maker","Cooler Unit"];
  const icons=["🐟","💡","🌊","❄️"];
  const watts=[0,0,0,0];
  const starts=[360,480,0,1380];
  const stops=[1320,1080,0,300];
  return {version:"3.2.8",ip:"--",wifi:false,ota:true,networkMode:"Preview / Disconnected",rssi:0,uptime:"--",time:"--",bootReason:"--",logCount:1,autoCount:3,scheduleCount:3,totalRuntime:0,totalEnergy:0,nextSchedule:"09:30 • Aquarium Lights",demo:true,
    relays:names.map((name,i)=>({id:i+1,name,icon:icons[i],state:false,mode:i===2?"MANUAL":"AUTO",emergency:false,manualOverride:false,watts:watts[i],runtime:0,
      schedules:[{enabled:i!==2,start:starts[i],stop:stops[i],days:127,fullDay:false},{enabled:false,start:0,stop:0,days:127,fullDay:false},{enabled:false,start:0,stop:0,days:127,fullDay:false},{enabled:false,start:0,stop:0,days:127,fullDay:false},{enabled:false,start:0,stop:0,days:127,fullDay:false},{enabled:false,start:0,stop:0,days:127,fullDay:false}] }))};
}
function syncBrowserTime(){
  const epoch=Math.floor(Date.now()/1000);
  fetch(`/api/time?epoch=${epoch}`,{cache:"no-store"}).catch(()=>{});
}
function loadLogs(){
  fetch("/api/logs?limit=300",{cache:"no-store"}).then(r=>r.json()).then(d=>{LOGS=d.logs||[];renderLogs();}).catch(()=>{
    if(!LOGS.length){LOGS=["-- | Activity log unavailable"]; }
    renderLogs();
  });
}
function loadStatus(){
  fetch("/api/status",{cache:"no-store"}).then(r=>r.json()).then(d=>{DATA=d;render()}).catch(()=>{
    DATA=demoStatus(); render();
    $("systemStatus").innerHTML='<b style="color:#f23d4b">● ESP32 disconnected</b><br><span style="color:var(--muted);font-size:12px">Preview data only — connect to the ESP32 IP to control real relays.</span>';
    $("systemInfo").textContent="4 devices • Preview mode • OTA: Ready when connected";
  });
}

function render(){
  if(!DATA)return;
  const active=DATA.relays.filter(r=>r.state).length;
  $("activeRelays").textContent=active+" / "+DATA.relays.length;
  $("totalRuntime").textContent=formatMs(DATA.totalRuntime);
  $("energy").textContent=DATA.totalEnergy.toFixed(3)+" kWh";
  $("energyDetail").textContent=(DATA.totalEnergy*1000).toFixed(2)+" Wh • "+(DATA.currentPower||0)+" W now";
  $("nextSchedule").textContent=DATA.nextSchedule||"--";
  $("footerConnection").textContent="ESP32 "+(DATA.wifi?"Connected":"Disconnected")+" | "+DATA.ip+" | FW "+DATA.version;
  const emergencies=DATA.relays.filter(r=>r.emergency).length;
  $("systemStatus").innerHTML=DATA.wifi?(emergencies?`<span style="color:#f23d4b">● Emergency OFF active</span>`:`<span style="color:#0b9856">● ESP32 Connected</span>`):`<span style="color:#f23d4b">● Wi-Fi disconnected</span>`;
  $("systemInfo").textContent=`${DATA.relays.length} devices • ${DATA.autoCount} AUTO • ${DATA.scheduleCount} schedules • OTA: ${DATA.ota?"Ready":"Not ready"} • Logs: ${DATA.logCount||0}/300`;
  $("networkInfo").textContent=`${DATA.networkMode} • Time: ${DATA.time}`;
  if($("statusIP"))$("statusIP").textContent=DATA.ip||"--";
  if($("statusRSSI"))$("statusRSSI").textContent=DATA.wifi?(DATA.rssi+" dBm"):"--";
  if($("statusUptime"))$("statusUptime").textContent=DATA.uptime||"--";
  $("logCountLabel").textContent=`(Showing latest ${DATA.logCount||0} events)`;

  let html="";
  DATA.relays.forEach(r=>{
    const activeSchedules=r.schedules.filter(s=>s.enabled);
    const scheduleText=r.mode==="AUTO"?(activeSchedules.length?activeSchedules.map(s=>s.fullDay?"24x7":minutesToTime(s.start)+" → "+minutesToTime(s.stop)).join(" • "):"No schedules"):"Manual control";
    const status=r.emergency?"🚨 Emergency OFF":(r.manualOverride?"Manual override":(r.state?"Running":"Stopped"));
    let days="";for(let d=0;d<7;d++){const mask=r.schedules.length?r.schedules[0].days:127;days+=`<button class="day ${mask&(1<<d)?"active":""}" onclick="toggleDay(${r.id},${d})">${DAYS[d]}</button>`}
    let next=r.nextAction||"—";
    html+=`<article class="relay">
      <div class="relay-head"><div class="icon">${escapeHtml(r.icon)}</div>
      <div class="title"><h2>${escapeHtml(r.name)} <span class="badge">Relay ${r.id}</span></h2><p>Relay ${r.id} • ${r.mode} mode</p></div>
      <div class="switch ${r.state?"on":""}" onclick="toggleRelay(${r.id},${r.state?"'off'":"'on'"})"></div></div>
      <div class="status ${r.state?"onText":"offText"}">● ${r.state?"Running":"Stopped"} <span class="mode-badge ${r.mode==="AUTO"?"auto-badge":"manual-badge"}">${r.mode}</span><span style="float:right;color:var(--muted);font-weight:400">Today<br><b style="color:var(--text);font-size:16px">${formatMs(r.runtime)}</b></span></div>
      <div class="meta">📅 <b>${r.mode==="AUTO"?"Schedule:":"Status:"}</b> ${escapeHtml(scheduleText)}${r.mode==="AUTO"?" • Mon-Sun":""} <span style="float:right">›</span></div>
      <div class="next-action">➜ Next Action: <span>${escapeHtml(next)}</span></div>
      <div class="actions"><button class="btn onBtn" onclick="toggleRelay(${r.id},'on')">⏻ ON</button><button class="btn offBtn" onclick="toggleRelay(${r.id},'off')">⏻ OFF</button><button class="btn outline" onclick="openSchedule(${r.id})">📅 Schedule</button><button class="btn outline" onclick="openNamesForRelay(${r.id})">⚙ Settings</button></div>
      ${r.emergency?`<button class="btn resume" onclick="resumeRelay(${r.id})">▶ Resume AUTO</button>`:`<button class="btn emergency" onclick="emergencyOff(${r.id})">🚨 Emergency OFF</button>`}
    </article>`;
  });
  $("relayGrid").innerHTML=html;

  const draftWatts={};
  DATA.relays.forEach(r=>{const el=$("watts"+r.id);if(el)draftWatts[r.id]=el.value});
  let p="";DATA.relays.forEach(r=>{const kwh=(r.watts/1000)*(r.runtime/3600000);const value=Object.prototype.hasOwnProperty.call(draftWatts,r.id)?draftWatts[r.id]:r.watts;
    p+=`<div class="power-card"><div class="power-head"><span>${escapeHtml(r.icon)} ${escapeHtml(r.name)}</span><span class="power-watt-badge">${r.watts}W</span></div>
      <label>Power Rating (0–5000W)</label><div style="display:flex;align-items:center;gap:6px"><input id="watts${r.id}" type="number" min="0" max="5000" value="${escapeAttr(value)}" oninput="syncWattSlider(${r.id},this.value)"><b>W</b></div>
      <input id="slider${r.id}" type="range" min="0" max="5000" value="${escapeAttr(value)}" oninput="syncWattInput(${r.id},this.value)"><div class="range-labels"><span>0W</span><span>2500W</span><span>5000W</span></div>
      <div class="cost">Today runtime: ${formatMs(r.runtime)} • Est. energy: ${kwh.toFixed(3)} kWh</div>
      <div class="power-actions"><button onclick="savePower(${r.id})">💾 Update</button><button class="reset" onclick="resetPower(${r.id})">Reset</button></div></div>`;
  });
  $("powerGrid").innerHTML=p;
}
function syncWattSlider(id,v){const s=$("slider"+id);if(s)s.value=v}
function syncWattInput(id,v){const i=$("watts"+id);if(i)i.value=v}
function openNamesForRelay(id){openNames();setTimeout(()=>{const e=$("name"+id);if(e)e.focus()},50)}
function formatMs(ms){let m=Math.floor(ms/60000),h=Math.floor(m/60);m%=60;return h?h+"h "+m+"m":m+"m"}
function minutesToTime(m){let h=Math.floor(m/60),mm=m%60;return String(h).padStart(2,"0")+":"+String(mm).padStart(2,"0")}
function timeToMinutes(v){let p=v.split(":");return(+p[0])*60+(+p[1])}
function toggleRelay(id,a){api(`/api/relay?id=${id}&action=${a}`).then(()=>{loadStatus();setTimeout(loadLogs,150);}).catch(e=>{if(confirm(e.message+"\n\nUse Emergency OFF instead?"))emergencyOff(id)})}
function emergencyOff(id){if(!confirm("Force this relay OFF and ignore schedules until Resume AUTO?"))return;api(`/api/relay?id=${id}&action=emergency`).then(()=>{loadStatus();setTimeout(loadLogs,150)})}
function resumeRelay(id){api(`/api/relay?id=${id}&action=resume`).then(()=>{loadStatus();setTimeout(loadLogs,150)})}
function allOff(){if(!confirm("Emergency OFF will isolate ALL relays until Resume AUTO. Continue?"))return;api("/api/alloff").then(()=>{loadStatus();setTimeout(loadLogs,150)})}
function resumeAll(){api("/api/resumeall").then(loadStatus)}
function openSchedule(id){
  const r=DATA.relays.find(x=>x.id===id);if(!r)return;$("scheduleTitle").textContent="📅 "+r.name+" Schedules";$("scheduleMode").value=r.mode;
  let html="";r.schedules.forEach((s,i)=>{let w="";for(let d=0;d<7;d++)w+=`<button class="${s.days&(1<<d)?"active":""}" onclick="toggleModalDay(${id},${i},${d})">${DAYS[d]}</button>`;
    html+=`<div class="schedule-card"><div class="schedule-top"><b>Schedule ${i+1}</b><label><input type="checkbox" id="en${i}" ${s.enabled?"checked":""}> Enabled</label></div>
      <div class="field"><label>Start</label><input id="st${i}" type="time" value="${minutesToTime(s.start)}"></div>
      <div class="field"><label>Stop</label><input id="sp${i}" type="time" value="${minutesToTime(s.stop)}"></div>
      <label><input type="checkbox" id="full${i}" ${s.fullDay?"checked":""}> 24x7 (00:00–00:00)</label>
      <div class="field"><label>Active Days</label><div class="week">${w}</div></div>
      <button class="save" style="width:100%;padding:9px;border:0;border-radius:7px" onclick="saveSchedule(${id},${i})">💾 Save Schedule ${i+1}</button></div>`;
  });$("scheduleList").innerHTML=html;$("scheduleModal").classList.add("show");
}
function toggleModalDay(id,slot,d){const r=DATA.relays.find(x=>x.id===id);if(!r)return;r.schedules[slot].days^=(1<<d);openSchedule(id)}
function saveSchedule(id,slot){
  const r=DATA.relays.find(x=>x.id===id),s=r.schedules[slot],full=$("full"+slot).checked;
  const start=full?0:timeToMinutes($("st"+slot).value),stop=full?0:timeToMinutes($("sp"+slot).value),enabled=$("en"+slot).checked;
  if(enabled&&s.days===0){alert("Select at least one day.");return}
  const p=new URLSearchParams({id,slot,enabled:enabled?"1":"0",start,stop,days:s.days,fullDay:full?"1":"0",mode:$("scheduleMode").value});
  api("/api/schedule",{method:"POST",body:p}).then(loadStatus).then(()=>openSchedule(id)).catch(e=>alert(e.message));
}
function toggleDay(id,d){
  const r=DATA.relays.find(x=>x.id===id);if(!r)return;const s=r.schedules[0],days=s.days^(1<<d);
  const p=new URLSearchParams({id,slot:0,enabled:s.enabled?"1":"0",start:s.start,stop:s.stop,days,fullDay:s.fullDay?"1":"0",mode:r.mode});
  api("/api/schedule",{method:"POST",body:p}).then(loadStatus);
}
function openNames(){$("nameFields").innerHTML=DATA.relays.map(r=>`<div class="field"><label>Relay ${r.id} Name</label><input id="name${r.id}" value="${escapeAttr(r.name)}"><label style="margin-top:7px">Icon</label><input id="icon${r.id}" value="${escapeAttr(r.icon)}"><button class="save" style="width:100%;padding:9px;border:0;border-radius:7px;margin-top:7px" onclick="saveDevice(${r.id})">💾 Save Relay ${r.id}</button></div>`).join("");$("namesModal").classList.add("show")}
function saveDevice(id){const p=new URLSearchParams({id,name:$("name"+id).value,icon:$("icon"+id).value});api("/api/device",{method:"POST",body:p}).then(loadStatus).catch(e=>alert(e.message))}
function wifiSettings(){$("wifiSSID").value="";$("wifiPassword").value="";$("wifiModal").classList.add("show")}
function saveWiFi(){const ssid=$("wifiSSID").value.trim(),pass=$("wifiPassword").value;if(!ssid){alert("SSID cannot be empty.");return}api("/api/wifi",{method:"POST",body:new URLSearchParams({ssid,password:pass})}).then(()=>{alert("Wi-Fi saved. ESP32 will restart.");closeModal("wifiModal")}).catch(e=>alert(e.message))}
function savePower(id){const w=parseInt($("watts"+id).value,10);if(!Number.isFinite(w)||w<0||w>5000){alert("Power must be 0–5000 W.");return}api(`/api/power?id=${id}&watts=${w}`,{method:"POST"}).then(()=>{if(DATA&&DATA.relays){const r=DATA.relays.find(x=>x.id===id);if(r)r.watts=w;}loadStatus()}).catch(e=>alert(e.message))}
function resetPower(id){api(`/api/power?id=${id}&reset=1`,{method:"POST"}).then(loadStatus)}
function scrollPower(){$("powerSection").scrollIntoView({behavior:"smooth"})}
function downloadBackup(){fetch("/api/status").then(r=>r.json()).then(d=>{d.backupCreated=new Date().toISOString();const blob=new Blob([JSON.stringify(d,null,2)],{type:"application/json"});const a=document.createElement("a");a.href=URL.createObjectURL(blob);a.download="aquarium-controller-backup.json";a.click();URL.revokeObjectURL(a.href)})}
const restoreInput=$("restoreInput");restoreInput.addEventListener("change",()=>{const file=restoreInput.files[0];if(!file)return;const reader=new FileReader();reader.onload=()=>{try{const d=JSON.parse(reader.result);if(!d.relays||d.relays.length!==DATA.relays.length)throw new Error("Backup relay count does not match.");restoreBackup(d)}catch(e){alert("Restore failed: "+e.message)}};reader.readAsText(file);restoreInput.value=""});
async function restoreBackup(d){if(!confirm("Restore names, icons, power settings and schedules?"))return;try{
  for(const r of d.relays){await api("/api/device",{method:"POST",body:new URLSearchParams({id:r.id,name:r.name,icon:r.icon})});await api(`/api/power?id=${r.id}&watts=${r.watts}`,{method:"POST"});
    for(let i=0;i<r.schedules.length;i++){const s=r.schedules[i];await api("/api/schedule",{method:"POST",body:new URLSearchParams({id:r.id,slot:i,enabled:s.enabled?"1":"0",start:s.start,stop:s.stop,days:s.days,fullDay:s.fullDay?"1":"0",mode:r.mode})})}
  }alert("Backup restored.");loadStatus()
}catch(e){alert("Restore stopped: "+e.message)}}
function resetController(){if(!confirm("Factory reset will erase schedules, names, power settings and Wi-Fi settings. Continue?"))return;api("/api/reset").then(()=>alert("ESP32 resetting...")).catch(e=>alert(e.message))}
function renderLogs(){
  const q=(($('logSearch')&&$('logSearch').value)||'').toLowerCase();
  const rows=LOGS.filter(x=>x.toLowerCase().includes(q)).slice().reverse();
  if(!rows.length){$('logs').innerHTML='<div style="padding:15px;color:var(--muted)">No matching activity.</div>';return;}
  let h='<table class="logtable"><thead><tr><th>Date & Time</th><th>Relay</th><th>Event</th><th>Reason</th><th>Details</th></tr></thead><tbody>';
  rows.forEach(x=>{
    const parts=x.split(' | ');const dt=parts[0]||'';const ev=(parts[1]||x);let relay='—',event=ev,reason='—',details='';
    const rm=ev.match(/\b(R[1-4])\b/);if(rm)relay=rm[1];
    const pm=ev.match(/\(([^)]+)\)/);if(pm)reason=pm[1];
    const nm=ev.match(/^(.*?)\s*\(([^)]+)\)/);if(nm)event=nm[1];
    if(event.includes(' | ')){const a=event.split(' | ');event=a[0];details=a.slice(1).join(' | ')}
    let cls=event.includes('ON')?'green':(event.includes('OFF')?'red':(event.includes('updated')?'blue':''));
    h+=`<tr><td>${escapeHtml(dt)}</td><td>${escapeHtml(relay)}</td><td class="${cls}">${escapeHtml(event)}</td><td>${escapeHtml(reason)}</td><td>${escapeHtml(details||event)}</td></tr>`;
  });h+='</tbody></table>';$('logs').innerHTML=h;
}
function filterLogs(){renderLogs()}
function exportLogs(){const csv="Date/Time,Event\n"+LOGS.map(x=>`"${x.replaceAll('"','""')}"`).join("\n");const blob=new Blob([csv],{type:"text/csv"});const a=document.createElement("a");a.href=URL.createObjectURL(blob);a.download="aquarium-activity-log.csv";a.click();URL.revokeObjectURL(a.href)}
function clearLogs(){if(confirm("Clear all 300 stored activity log entries?"))api("/api/logs/clear").then(()=>{LOGS=[];renderLogs();loadStatus()})}
function openDefaults(){alert("Each relay supports 6 schedules. Use 24x7 for continuous equipment such as a filter or air pump. AUTO controls schedules; MANUAL provides direct control. Emergency OFF isolates a relay until Resume AUTO.")}
function otaInfo(){alert("OTA is enabled. From a computer on the same Wi-Fi/LAN, select Aquarium-Controller in the Arduino IDE network ports and upload. OTA password: aquarium-ota. OTA does not provide Internet access from outside your home.")}
function closeModal(id){$(id).classList.remove("show")}
function toggleTheme(){document.body.classList.toggle("dark");localStorage.setItem("aqTheme",document.body.classList.contains("dark")?"dark":"light")}
function escapeHtml(v){return String(v).replaceAll("&","&amp;").replaceAll("<","&lt;").replaceAll(">","&gt;").replaceAll('"',"&quot;").replaceAll("'","&#039;")}
function escapeAttr(v){return escapeHtml(v)}
if(localStorage.getItem("aqTheme")==="dark")document.body.classList.add("dark");
syncBrowserTime();loadStatus();loadLogs();setInterval(loadStatus,2000);setInterval(syncBrowserTime,30000);setInterval(loadLogs,5000);
</script>
</body>
</html>
)HTML";

// ============================================================
// HELPERS
// ============================================================
String urlDecode(String s) {
  String out; out.reserve(s.length());
  for(size_t i=0;i<s.length();i++){
    char c=s[i];
    if(c=='+') out+=' ';
    else if(c=='%' && i+2<s.length()){
      char h1=s[i+1],h2=s[i+2];
      auto hv=[](char h)->int{
        if(h>='0'&&h<='9')return h-'0';
        if(h>='A'&&h<='F')return h-'A'+10;
        if(h>='a'&&h<='f')return h-'a'+10;
        return 0;
      };
      out+=char((hv(h1)<<4)|hv(h2));i+=2;
    }else out+=c;
  }
  return out;
}

String jsonEscape(String s){
  s.replace("\\","\\\\");s.replace("\"","\\\"");s.replace("\n","\\n");s.replace("\r","\\r");return s;
}
String formatMinutes(uint16_t m){char b[8];snprintf(b,sizeof(b),"%02d:%02d",m/60,m%60);return String(b);}
String formatDuration(uint64_t ms){uint64_t min=ms/60000ULL,h=min/60ULL;min%=60ULL;return h?String((uint32_t)h)+"h "+String((uint32_t)min)+"m":String((uint32_t)min)+"m";}
// Return India Standard Time deterministically from the ESP32 epoch.
// We intentionally do not depend on the ESP32 TZ environment for scheduling.
// NTP/browser synchronization sets the UTC epoch; +05:30 is applied here.
bool getTimeInfo(struct tm &t){
  time_t now=time(nullptr);
  if(now < 1700000000) return false;
  time_t indiaEpoch=now + 19800; // IST = UTC + 5h 30m
  gmtime_r(&indiaEpoch,&t);
  cachedTimeInfo=t;
  cachedTimeValid=true;
  return true;
}
int currentMinutes(){struct tm t;if(!getTimeInfo(t))return -1;return t.tm_hour*60+t.tm_min;}
int currentWeekday(){struct tm t;if(!getTimeInfo(t))return -1;return(t.tm_wday+6)%7;}
String currentDateTime(){
  struct tm t;
  if(!getTimeInfo(t))return "--";
  char b[32];
  strftime(b,sizeof(b),"%d-%m-%Y %H:%M:%S",&t);
  return String(b);
}
void updateCachedClock(){
  if(millis()-lastClockUpdate<1000)return;
  lastClockUpdate=millis();
  struct tm t;
  if(getTimeInfo(t)){
    cachedTimeInfo=t;
    cachedTimeValid=true;
    schedulerTimeReady=true;
  }
}

String bootReason(){
  switch(esp_reset_reason()){
    case ESP_RST_POWERON: return "Power On";
    case ESP_RST_EXT: return "External Reset";
    case ESP_RST_SW: return "Software Reset";
    case ESP_RST_PANIC: return "Panic";
    case ESP_RST_INT_WDT: return "Interrupt WDT";
    case ESP_RST_TASK_WDT: return "Task WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "Deep Sleep";
    case ESP_RST_BROWNOUT: return "Brownout";
    default: return "Unknown";
  }
}

void rewriteLogFile(){
  File f=LittleFS.open(LOG_FILE,"w");
  if(!f) return;
  for(int i=0;i<logCount;i++) f.println(activityLogs[i]);
  f.close();
}

void loadLogsFromFS(){
  logCount=0;
  if(!LittleFS.exists(LOG_FILE)) return;
  File f=LittleFS.open(LOG_FILE,"r");
  if(!f) return;
  while(f.available()){
    String line=f.readStringUntil('\n');
    line.trim();
    if(!line.length()) continue;
    if(logCount<MAX_LOGS) activityLogs[logCount++]=line;
    else { for(int i=1;i<MAX_LOGS;i++) activityLogs[i-1]=activityLogs[i]; activityLogs[MAX_LOGS-1]=line; }
  }
  f.close();
}

void persistRuntime(){
  for(int i=0;i<RELAY_COUNT;i++){
    char k[24];
    snprintf(k,sizeof(k),"rtD%d",i); prefs.putULong64(k,relays[i].runtimeTodayMs);
    snprintf(k,sizeof(k),"rtT%d",i); prefs.putULong64(k,relays[i].runtimeTotalMs);
  }
  String dt=currentDateTime();
  if(dt.length()) prefs.putString("rtDate",dt.substring(0,10));
}

void addLog(const String &msg){
  String dt=currentDateTime();
  if(dt=="--" || dt.length()==0) dt="TIME-NOT-SYNCED";
  String e=dt+" | "+msg;
  if(logCount<MAX_LOGS){
    activityLogs[logCount++]=e;
    File f=LittleFS.open(LOG_FILE,"a"); if(f){f.println(e);f.close();}
  }else{
    for(int i=1;i<MAX_LOGS;i++) activityLogs[i-1]=activityLogs[i];
    activityLogs[MAX_LOGS-1]=e;
    rewriteLogFile();
  }
  Serial.println(e);
}

void setRelayHardware(int id,bool on){if(id<0||id>=RELAY_COUNT)return;digitalWrite(relayPins[id],on?RELAY_ON:RELAY_OFF);}

uint64_t currentRuntime(int id){
  if(id<0||id>=RELAY_COUNT)return 0;
  uint64_t r=relays[id].runtimeTodayMs;
  if(relays[id].state)r+=(uint64_t)(millis()-relays[id].stateStartedMillis);
  return r;
}

void setRelayState(int id,bool on,const String &reason){
  if(id<0||id>=RELAY_COUNT)return;
  RelayConfig&r=relays[id];
  if(r.state==on)return;
  if(r.state){uint64_t elapsed=(uint64_t)(millis()-r.stateStartedMillis);r.runtimeTodayMs+=elapsed;r.runtimeTotalMs+=elapsed;}
  r.state=on;r.stateStartedMillis=on?millis():0;setRelayHardware(id,on);
  if(!on) runtimeDirty = true;
  addLog(r.name+" "+(on?"ON":"OFF")+" ("+reason+")");
}

// ============================================================
// SCHEDULE ENGINE
// ============================================================
bool slotActive(const ScheduleSlot&s,int now,int today){
  if(!s.enabled||s.days==0)return false;
  if(s.startMinutes==0&&s.stopMinutes==0)return(s.days&(1<<today)); // 24x7

  if(s.startMinutes < s.stopMinutes){
    return (s.days&(1<<today)) && now>=s.startMinutes && now<s.stopMinutes;
  }

  // Overnight schedule, including start == stop as a 24-hour interval.
  if(s.startMinutes == s.stopMinutes){
    return (s.days&(1<<today));
  }

  if(now>=s.startMinutes) return (s.days&(1<<today));
  if(now<s.stopMinutes){
    int prev=(today+6)%7;
    return (s.days&(1<<prev));
  }
  return false;
}

bool anyScheduleActive(int id){
  if(id<0||id>=RELAY_COUNT||!relays[id].autoMode)return false;
  int now=currentMinutes(),day=currentWeekday();
  if(now<0||day<0)return false;
  for(int j=0;j<MAX_SCHEDULES;j++){
    if(slotActive(relays[id].schedules[j],now,day))return true;
  }
  return false;
}

void processSchedules(){
  // Deterministic scheduler: run frequently, but evaluate the actual IST
  // clock. No HTTP/NVS operation is performed here.
  if(millis()-lastScheduleCheck<200)return;
  lastScheduleCheck=millis();

  struct tm t;
  if(!getTimeInfo(t)){
    schedulerTimeReady=false;
    return;
  }
  schedulerTimeReady=true;

  int today=(t.tm_wday+6)%7;
  if(lastWeekday<0)lastWeekday=today;

  if(today!=lastWeekday){
    for(int i=0;i<RELAY_COUNT;i++){
      relays[i].runtimeTodayMs=0;
      if(relays[i].state)relays[i].stateStartedMillis=millis();
    }
    lastWeekday=today;
  }

  int now=t.tm_hour*60+t.tm_min;

  for(int i=0;i<RELAY_COUNT;i++){
    RelayConfig&r=relays[i];
    if(!r.autoMode||r.emergencyOff)continue;

    bool active=false;
    for(int j=0;j<MAX_SCHEDULES;j++){
      if(slotActive(r.schedules[j],now,today)){active=true;break;}
    }

    // In AUTO, an active schedule owns the relay. When no schedule is active,
    // return it OFF unless the user explicitly created a manual override.
    if(active){
      r.manualOverride=false;
      if(!r.state)setRelayState(i,true,"Schedule ON");
    }else if(!r.manualOverride){
      if(r.state)setRelayState(i,false,"Schedule OFF");
    }
  }
}

// ============================================================
// PREFERENCES
// ============================================================
void clearSchedules(int id){
  for(int j=0;j<MAX_SCHEDULES;j++){relays[id].schedules[j].enabled=false;relays[id].schedules[j].startMinutes=0;relays[id].schedules[j].stopMinutes=0;relays[id].schedules[j].days=0x7F;}
}

void setDefaults(){
  for(int i=0;i<RELAY_COUNT;i++){
    relays[i].name=DEFAULT_NAMES[i];relays[i].icon=DEFAULT_ICONS[i];relays[i].state=false;relays[i].autoMode=(i<2);
    relays[i].manualOverride=false;relays[i].emergencyOff=false;relays[i].emergencySavedState=false;relays[i].emergencySavedManualOverride=false;relays[i].watts=DEFAULT_WATTS[i];
    relays[i].runtimeTodayMs=0;relays[i].runtimeTotalMs=0;relays[i].stateStartedMillis=0;clearSchedules(i);
  }
  relays[0].schedules[0]={true,8*60,14*60,0x7F};
  relays[1].schedules[0]={true,10*60,16*60,0x7F};
}

void saveRelay(int id){
  if(id<0||id>=RELAY_COUNT)return;char k[24];
  snprintf(k,sizeof(k),"name%d",id);prefs.putString(k,relays[id].name);
  snprintf(k,sizeof(k),"icon%d",id);prefs.putString(k,relays[id].icon);
  snprintf(k,sizeof(k),"auto%d",id);prefs.putBool(k,relays[id].autoMode);
  snprintf(k,sizeof(k),"em%d",id);prefs.putBool(k,relays[id].emergencyOff);
  snprintf(k,sizeof(k),"es%d",id);prefs.putBool(k,relays[id].emergencySavedState);
  snprintf(k,sizeof(k),"eo%d",id);prefs.putBool(k,relays[id].emergencySavedManualOverride);
  snprintf(k,sizeof(k),"w%d",id);prefs.putUShort(k,relays[id].watts);
  for(int j=0;j<MAX_SCHEDULES;j++){
    snprintf(k,sizeof(k),"e%d_%d",id,j);prefs.putBool(k,relays[id].schedules[j].enabled);
    snprintf(k,sizeof(k),"s%d_%d",id,j);prefs.putUShort(k,relays[id].schedules[j].startMinutes);
    snprintf(k,sizeof(k),"t%d_%d",id,j);prefs.putUShort(k,relays[id].schedules[j].stopMinutes);
    snprintf(k,sizeof(k),"d%d_%d",id,j);prefs.putUChar(k,relays[id].schedules[j].days);
  }
}

// Fast persistence used by relay/emergency actions.
// Manual ON/OFF does not need a full configuration rewrite.
void saveRelayRuntimeState(int id){
  if(id<0||id>=RELAY_COUNT)return;
  char k[24];
  snprintf(k,sizeof(k),"em%d",id);
  prefs.putBool(k,relays[id].emergencyOff);
  snprintf(k,sizeof(k),"es%d",id);
  prefs.putBool(k,relays[id].emergencySavedState);
  snprintf(k,sizeof(k),"eo%d",id);
  prefs.putBool(k,relays[id].emergencySavedManualOverride);
  snprintf(k,sizeof(k),"auto%d",id);
  prefs.putBool(k,relays[id].autoMode);
}

void loadSettings(){
  setDefaults();prefs.begin("aquarium",false);
  // One-time migration: clear only the old firmware's untouched defaults.
  // Any wattage the user had already customized is preserved.
  if(!prefs.getBool("powerV23",false)){
    const uint16_t OLD_WATTS[RELAY_COUNT]={50,60,150,200};
    for(int i=0;i<RELAY_COUNT;i++){
      char wk[24]; snprintf(wk,sizeof(wk),"w%d",i);
      uint16_t saved=prefs.getUShort(wk,DEFAULT_WATTS[i]);
      if(saved==OLD_WATTS[i]){ relays[i].watts=0; prefs.putUShort(wk,0); }
    }
    prefs.putBool("powerV23",true);
  }
  wifiSSID=prefs.getString("ssid",DEFAULT_WIFI_SSID);wifiPassword=prefs.getString("pass",DEFAULT_WIFI_PASSWORD);
  for(int i=0;i<RELAY_COUNT;i++){
    char k[24];
    snprintf(k,sizeof(k),"name%d",i);relays[i].name=prefs.getString(k,relays[i].name);
    snprintf(k,sizeof(k),"icon%d",i);relays[i].icon=prefs.getString(k,relays[i].icon);
    snprintf(k,sizeof(k),"auto%d",i);relays[i].autoMode=prefs.getBool(k,relays[i].autoMode);
    snprintf(k,sizeof(k),"em%d",i);relays[i].emergencyOff=prefs.getBool(k,false);
    snprintf(k,sizeof(k),"es%d",i);relays[i].emergencySavedState=prefs.getBool(k,false);
    snprintf(k,sizeof(k),"eo%d",i);relays[i].emergencySavedManualOverride=prefs.getBool(k,false);
    snprintf(k,sizeof(k),"w%d",i);relays[i].watts=prefs.getUShort(k,relays[i].watts);
    for(int j=0;j<MAX_SCHEDULES;j++){
      snprintf(k,sizeof(k),"e%d_%d",i,j);relays[i].schedules[j].enabled=prefs.getBool(k,relays[i].schedules[j].enabled);
      snprintf(k,sizeof(k),"s%d_%d",i,j);relays[i].schedules[j].startMinutes=prefs.getUShort(k,relays[i].schedules[j].startMinutes);
      snprintf(k,sizeof(k),"t%d_%d",i,j);relays[i].schedules[j].stopMinutes=prefs.getUShort(k,relays[i].schedules[j].stopMinutes);
      snprintf(k,sizeof(k),"d%d_%d",i,j);relays[i].schedules[j].days=prefs.getUChar(k,relays[i].schedules[j].days);
    }
    snprintf(k,sizeof(k),"rtT%d",i); relays[i].runtimeTotalMs=prefs.getULong64(k,0);
  }
}

// ============================================================
// WIFI / OTA
// ============================================================
void restoreTodayRuntime(){
  String savedDate=prefs.getString("rtDate","");
  String today=currentDateTime().substring(0,10);
  if(savedDate.length() && savedDate==today){
    for(int i=0;i<RELAY_COUNT;i++){ char k[24]; snprintf(k,sizeof(k),"rtD%d",i); relays[i].runtimeTodayMs=prefs.getULong64(k,0); }
  }
}

void startFallbackAP(){
  fallbackAP=true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(FALLBACK_AP_SSID,FALLBACK_AP_PASSWORD);
  Serial.println("Wi-Fi unavailable. Fallback AP started.");
  Serial.print("AP SSID: ");Serial.println(FALLBACK_AP_SSID);
  Serial.print("AP IP: ");Serial.println(WiFi.softAPIP());
}

void connectWiFi(){
  fallbackAP=false;
  WiFi.mode(WIFI_STA);WiFi.setHostname(DEVICE_HOSTNAME);WiFi.begin(wifiSSID.c_str(),wifiPassword.c_str());
  Serial.print("Connecting to Wi-Fi");
  for(int i=0;i<40&&WiFi.status()!=WL_CONNECTED;i++){delay(500);Serial.print(".");}
  Serial.println();
  if(WiFi.status()==WL_CONNECTED){
    Serial.print("IP: ");Serial.println(WiFi.localIP());
    // Sync the ESP32 epoch with NTP. The application converts epoch -> IST
    // explicitly in getTimeInfo(), so scheduler/log time is independent of TZ.
    configTime(0,0,"pool.ntp.org","time.nist.gov","time.google.com");
    unsigned long waitStart=millis();
    while(time(nullptr)<1700000000 && millis()-waitStart<10000){delay(100);}
    struct tm t;
    if(getTimeInfo(t)){
      cachedTimeInfo=t;
      cachedTimeValid=true;
      schedulerTimeReady=true;
      Serial.print("IST: ");Serial.println(currentDateTime());
    }else{
      Serial.println("WARNING: NTP time sync not available yet; browser time sync can initialise the clock.");
    }
    if(MDNS.begin(DEVICE_HOSTNAME)){MDNS.addService("http","tcp",80);Serial.print("mDNS: http://");Serial.print(DEVICE_HOSTNAME);Serial.println(".local/");}
  }else{
    startFallbackAP();
  }
}

void setupOTA(){
  ArduinoOTA.setHostname(DEVICE_HOSTNAME);
  ArduinoOTA.setPassword("aquarium-ota");
  ArduinoOTA.onStart([](){Serial.println("OTA update started - forcing relays OFF");for(int i=0;i<RELAY_COUNT;i++){relays[i].state=false;setRelayHardware(i,false);}});
  ArduinoOTA.onEnd([](){Serial.println("OTA update complete");});
  ArduinoOTA.onProgress([](unsigned int p,unsigned int total){Serial.printf("OTA %u%%\r",(p*100)/total);});
  ArduinoOTA.onError([](ota_error_t e){Serial.printf("\nOTA error[%u]\n",e);});
  ArduinoOTA.begin();
}

// ============================================================
// STATUS JSON
// ============================================================
String findNextSchedule();
String nextActionForRelay(int id);

String uptimeString(){
  uint64_t sec=(millis()-bootMillis)/1000ULL;
  uint32_t d=sec/86400UL;sec%=86400UL;uint32_t h=sec/3600UL;sec%=3600UL;uint32_t m=sec/60UL;sec%=60UL;
  return String(d)+"d "+String(h)+"h "+String(m)+"m "+String((uint32_t)sec)+"s";
}

String networkModeString(){
  if(fallbackAP) return WiFi.status()==WL_CONNECTED?"Wi-Fi + Setup AP":"Setup AP";
  return WiFi.status()==WL_CONNECTED?"Wi-Fi STA":"Wi-Fi disconnected";
}

String buildStatusJSON(){
  String j="{";j+="\"version\":\""+String(FW_VERSION)+"\",";
  String ip=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString();
  j+="\"ip\":\""+ip+"\",";
  j+="\"wifi\":"+(WiFi.status()==WL_CONNECTED?String("true"):String("false"))+",";
  j+="\"ota\":true,\"networkMode\":\""+jsonEscape(networkModeString())+"\",";
  j+="\"rssi\":"+String(WiFi.status()==WL_CONNECTED?WiFi.RSSI():0)+",";
  j+="\"uptime\":\""+jsonEscape(uptimeString())+"\",";
  j+="\"time\":\""+jsonEscape(currentDateTime())+"\",";
  j+="\"bootReason\":\""+jsonEscape(bootReason())+"\",";
  j+="\"logCount\":"+String(logCount)+",";
  int autoCount=0,scheduleCount=0;uint64_t totalRuntime=0;double energy=0;uint32_t currentPower=0;
  for(int i=0;i<RELAY_COUNT;i++){if(relays[i].autoMode)autoCount++;for(int k=0;k<MAX_SCHEDULES;k++)if(relays[i].schedules[k].enabled)scheduleCount++;uint64_t rt=currentRuntime(i);totalRuntime+=rt;energy+=(double)relays[i].watts*(double)rt/3600000000.0;if(relays[i].state)currentPower+=relays[i].watts;}
  j+="\"autoCount\":"+String(autoCount)+",\"scheduleCount\":"+String(scheduleCount)+",";
  j+="\"totalRuntime\":"+String((uint32_t)totalRuntime)+",\"totalEnergy\":"+String(energy,6)+",\"currentPower\":"+String(currentPower)+",";
  j+="\"nextSchedule\":\""+jsonEscape(findNextSchedule())+"\",\"relays\":[";
  for(int i=0;i<RELAY_COUNT;i++){
    if(i)j+=",";
    RelayConfig&r=relays[i];j+="{";
    j+="\"id\":"+String(i+1)+",\"name\":\""+jsonEscape(r.name)+"\",\"icon\":\""+jsonEscape(r.icon)+"\",";
    j+="\"state\":"+(r.state?String("true"):String("false"))+",\"mode\":\""+String(r.autoMode?"AUTO":"MANUAL")+"\",";
    j+="\"emergency\":"+(r.emergencyOff?String("true"):String("false"))+",\"manualOverride\":"+(r.manualOverride?String("true"):String("false"))+",";
    j+="\"watts\":"+String(r.watts)+",\"runtime\":"+String((uint32_t)currentRuntime(i))+",\"nextAction\":\""+jsonEscape(nextActionForRelay(i))+"\",\"schedules\":[";
    for(int k=0;k<MAX_SCHEDULES;k++){if(k)j+=",";ScheduleSlot&s=r.schedules[k];j+="{\"enabled\":"+(s.enabled?String("true"):String("false"))+",\"start\":"+String(s.startMinutes)+",\"stop\":"+String(s.stopMinutes)+",\"days\":"+String(s.days)+",\"fullDay\":"+(s.startMinutes==0&&s.stopMinutes==0?String("true"):String("false"))+"}";}
    j+="]}";
  }
  j+="]}";return j;
}

String findNextSchedule(){
  int now=currentMinutes(),today=currentWeekday();
  if(now<0||today<0)return "--";

  int bestDelta=10081;
  String result="";

  for(int i=0;i<RELAY_COUNT;i++){
    if(!relays[i].autoMode||relays[i].emergencyOff)continue;
    for(int s=0;s<MAX_SCHEDULES;s++){
      ScheduleSlot&x=relays[i].schedules[s];
      if(!x.enabled||x.days==0)continue;

      // 24x7 has no upcoming start to display.
      if(x.startMinutes==0&&x.stopMinutes==0)continue;

      for(int d=0;d<7;d++){
        if(!(x.days&(1<<d)))continue;
        int delta=((d-today+7)%7)*1440 + x.startMinutes - now;
        if(delta<=0)delta+=10080;
        if(delta<bestDelta){
          bestDelta=delta;
          result=formatMinutes(x.startMinutes)+" • "+relays[i].name;
        }
      }
    }
  }
  return result.length()?result:"--";
}

String nextActionForRelay(int id){
  if(id<0||id>=RELAY_COUNT||!relays[id].autoMode||relays[id].emergencyOff)return "—";
  int now=currentMinutes(),today=currentWeekday();
  if(now<0||today<0)return "—";

  // If currently active, find the earliest applicable stop boundary.
  bool activeNow=anyScheduleActive(id);
  int bestDelta=10081;
  uint16_t bestTime=0;
  bool bestIsOn=!activeNow;

  for(int s=0;s<MAX_SCHEDULES;s++){
    ScheduleSlot&x=relays[id].schedules[s];
    if(!x.enabled||x.days==0)continue;
    if(x.startMinutes==0&&x.stopMinutes==0)continue;

    if(activeNow){
      // Search future stop times for schedules active now, including overnight.
      for(int dayOffset=0;dayOffset<=1;dayOffset++){
        int day=(today+dayOffset)%7;
        int stop=x.stopMinutes;
        if(x.startMinutes<x.stopMinutes){
          if(dayOffset==0 && (x.days&(1<<today)) && now<x.stopMinutes && now>=x.startMinutes){
            int delta=x.stopMinutes-now;
            if(delta<bestDelta){bestDelta=delta;bestTime=x.stopMinutes;bestIsOn=false;}
          }
        }else if(x.startMinutes>x.stopMinutes){
          if(dayOffset==0 && (x.days&(1<<today)) && now>=x.startMinutes){
            int delta=(1440-now)+stop;
            if(delta<bestDelta){bestDelta=delta;bestTime=stop;bestIsOn=false;}
          }else if(dayOffset==0 && (x.days&(1<<((today+6)%7))) && now<stop){
            int delta=stop-now;
            if(delta<bestDelta){bestDelta=delta;bestTime=stop;bestIsOn=false;}
          }
        }
      }
    }else{
      for(int d=0;d<7;d++){
        if(!(x.days&(1<<d)))continue;
        int delta=((d-today+7)%7)*1440+x.startMinutes-now;
        if(delta<=0)delta+=10080;
        if(delta<bestDelta){bestDelta=delta;bestTime=x.startMinutes;bestIsOn=true;}
      }
    }
  }

  if(bestDelta>10080)return "—";
  return String(bestIsOn?"ON at ":"OFF at ")+formatMinutes(bestTime);
}

// ============================================================
// API
// ============================================================
void handleBrowserTime(){
  if(!server.hasArg("epoch")){server.send(400,"text/plain","Missing epoch");return;}
  long long epoch=server.arg("epoch").toInt();
  if(epoch<1700000000LL){server.send(400,"text/plain","Invalid epoch");return;}
  struct timeval tv;
  tv.tv_sec=(time_t)epoch;
  tv.tv_usec=0;
  settimeofday(&tv,nullptr);
  struct tm t;
  if(getTimeInfo(t)){
    cachedTimeInfo=t;
    cachedTimeValid=true;
    schedulerTimeReady=true;
  }
  server.sendHeader("Cache-Control","no-store");
  server.send(200,"text/plain","OK");
}

void handleStatus(){
  server.sendHeader("Cache-Control","no-store");
  server.sendHeader("Connection","close");
  server.send(200,"application/json",buildStatusJSON());
}

void handleRelay(){
  if(!server.hasArg("id")||!server.hasArg("action")){server.send(400,"text/plain","Missing parameters");return;}
  int id=server.arg("id").toInt()-1;String a=server.arg("action");if(id<0||id>=RELAY_COUNT){server.send(400,"text/plain","Invalid relay");return;}
  RelayConfig&r=relays[id];
  if(r.emergencyOff&&a!="resume"){server.send(409,"text/plain","Emergency OFF is active. Resume AUTO first.");return;}
  if(a=="on"||a=="off"){
    if(r.autoMode&&anyScheduleActive(id)){server.send(409,"text/plain","AUTO schedule is currently active. Use Emergency OFF to force a manual OFF.");return;}
    r.manualOverride=r.autoMode;setRelayState(id,a=="on","Manual");server.sendHeader("Connection","close");server.send(200,"text/plain","OK");return;
  }
  if(a=="emergency"){
    // Capture the exact pre-emergency state so Resume restores what the user
    // had before Emergency OFF, rather than recalculating it from the clock.
    r.emergencySavedState=r.state;
    r.emergencySavedManualOverride=r.manualOverride;
    r.emergencyOff=true;
    setRelayState(id,false,"Emergency OFF");
    saveRelayRuntimeState(id);
    server.sendHeader("Connection","close");server.send(200,"text/plain","OK");return;
  }
  if(a=="resume"){
    r.emergencyOff=false;
    r.manualOverride=r.emergencySavedManualOverride;
    bool restoreState=r.emergencySavedState;
    if(r.autoMode && !r.manualOverride){
      // If the saved state came from an active schedule, restoring it is
      // correct. Otherwise restore the original OFF state.
      restoreState=r.emergencySavedState;
    }
    setRelayState(id,restoreState,"Emergency Resume");
    r.emergencySavedState=false;
    r.emergencySavedManualOverride=false;
    saveRelayRuntimeState(id);
    addLog(r.name+" resumed from Emergency OFF");
    server.sendHeader("Connection","close");server.send(200,"text/plain","OK");return;
  }
  server.send(400,"text/plain","Invalid action");
}

void handleAllOff(){
  for(int i=0;i<RELAY_COUNT;i++){
    relays[i].emergencySavedState=relays[i].state;
    relays[i].emergencySavedManualOverride=relays[i].manualOverride;
    relays[i].emergencyOff=true;
    setRelayState(i,false,"Emergency All OFF");
    saveRelayRuntimeState(i);
  }
  addLog("All relays Emergency OFF");
  server.send(200,"text/plain","OK");
}
void handleResumeAll(){
  for(int i=0;i<RELAY_COUNT;i++){
    relays[i].emergencyOff=false;
    relays[i].manualOverride=relays[i].emergencySavedManualOverride;
    bool restoreState=relays[i].emergencySavedState;
    setRelayState(i,restoreState,"Emergency Resume");
    relays[i].emergencySavedState=false;
    relays[i].emergencySavedManualOverride=false;
    saveRelayRuntimeState(i);
  }
  addLog("All relays resumed from Emergency OFF");
  server.send(200,"text/plain","OK");
}

void handleSchedule(){
  if(!server.hasArg("id")||!server.hasArg("slot")||!server.hasArg("enabled")||!server.hasArg("start")||!server.hasArg("stop")||!server.hasArg("days")){server.send(400,"text/plain","Missing parameters");return;}
  int id=server.arg("id").toInt()-1,slot=server.arg("slot").toInt();if(id<0||id>=RELAY_COUNT||slot<0||slot>=MAX_SCHEDULES){server.send(400,"text/plain","Invalid relay or slot");return;}
  RelayConfig&r=relays[id];ScheduleSlot&s=r.schedules[slot];s.enabled=server.arg("enabled")=="1";s.startMinutes=(uint16_t)constrain(server.arg("start").toInt(),0,1439);s.stopMinutes=(uint16_t)constrain(server.arg("stop").toInt(),0,1439);s.days=(uint8_t)constrain(server.arg("days").toInt(),0,127);
  if(s.enabled&&s.days==0){server.send(400,"text/plain","Select at least one day");return;}if(server.hasArg("fullDay")&&server.arg("fullDay")=="1"){s.startMinutes=0;s.stopMinutes=0;}
  if(server.hasArg("mode"))r.autoMode=server.arg("mode")=="AUTO";r.manualOverride=false;saveRelay(id);addLog(r.name+" schedule "+String(slot+1)+" updated");
  if(r.autoMode&&!r.emergencyOff)setRelayState(id,anyScheduleActive(id),"Schedule update");server.send(200,"text/plain","OK");
}

void handleDevice(){
  if(!server.hasArg("id")||!server.hasArg("name")||!server.hasArg("icon")){server.send(400,"text/plain","Missing parameters");return;}
  int id=server.arg("id").toInt()-1;if(id<0||id>=RELAY_COUNT){server.send(400,"text/plain","Invalid relay");return;}
  relays[id].name=urlDecode(server.arg("name"));relays[id].icon=urlDecode(server.arg("icon"));if(relays[id].name.length()==0)relays[id].name="Relay "+String(id+1);
  saveRelay(id);addLog("Relay "+String(id+1)+" name/icon updated");server.send(200,"text/plain","OK");
}

void handlePower(){
  if(!server.hasArg("id")){server.send(400,"text/plain","Missing id");return;}int id=server.arg("id").toInt()-1;if(id<0||id>=RELAY_COUNT){server.send(400,"text/plain","Invalid relay");return;}
  if(server.hasArg("reset")&&server.arg("reset")=="1")relays[id].watts=DEFAULT_WATTS[id];else if(server.hasArg("watts"))relays[id].watts=(uint16_t)constrain(server.arg("watts").toInt(),0,5000);else{server.send(400,"text/plain","Missing watts");return;}
  saveRelay(id);addLog(relays[id].name+" power rating updated: "+String(relays[id].watts)+"W");server.sendHeader("Connection","close");server.send(200,"text/plain","OK");
}

void handleWiFi(){
  if(!server.hasArg("ssid")||!server.hasArg("password")){server.send(400,"text/plain","Missing Wi-Fi parameters");return;}
  String ssid=urlDecode(server.arg("ssid")),pass=urlDecode(server.arg("password"));if(ssid.length()==0){server.send(400,"text/plain","SSID cannot be empty");return;}
  prefs.putString("ssid",ssid);prefs.putString("pass",pass);server.send(200,"text/plain","Wi-Fi saved. Restarting...");delay(800);ESP.restart();
}

void handleLogs(){
  int limit=server.hasArg("limit")?constrain(server.arg("limit").toInt(),1,MAX_LOGS):MAX_LOGS;
  int first=max(0,(int)logCount-limit);
  String j; j.reserve((logCount-first)*70+32); j="{\"logs\":[";
  for(int i=first;i<logCount;i++){if(i>first)j+=",";j+="\""+jsonEscape(activityLogs[i])+"\"";}
  j+="]}"; server.sendHeader("Cache-Control","no-store");server.sendHeader("Connection","close");server.send(200,"application/json",j);
}
void handleClearLogs(){logCount=0;LittleFS.remove(LOG_FILE);server.send(200,"text/plain","OK");}
void handleReset(){for(int i=0;i<RELAY_COUNT;i++)setRelayHardware(i,false);LittleFS.remove(LOG_FILE);prefs.clear();server.send(200,"text/plain","Factory reset. Restarting...");delay(800);ESP.restart();}

// ============================================================
// SETUP / LOOP
// ============================================================
void setup(){
  bootMillis=millis();
  Serial.begin(115200);delay(300);
  Serial.println("\n====================================");Serial.println(" Aquarium Controller v3.2.8");Serial.println("====================================");

  // Startup safety: every relay OFF before Wi-Fi/settings initialization.
  for(int i=0;i<RELAY_COUNT;i++){pinMode(relayPins[i],OUTPUT);digitalWrite(relayPins[i],RELAY_OFF);}
  loadSettings();
  if(!LittleFS.begin(true)) Serial.println("WARNING: LittleFS unavailable; activity log will not survive reboot.");
  else loadLogsFromFS();

  // Hardware remains OFF after reboot. Scheduler will decide the state once time is available.
  for(int i=0;i<RELAY_COUNT;i++){relays[i].state=false;relays[i].stateStartedMillis=0;}

  connectWiFi();
  restoreTodayRuntime();
  lastRuntimeSave=millis();
  runtimeDirty=false;
  setupOTA();

  server.on("/",HTTP_GET,[](){server.send_P(200,"text/html",INDEX_HTML);});
  server.on("/api/time",HTTP_GET,handleBrowserTime);
  server.on("/api/status",HTTP_GET,handleStatus);
  server.on("/api/relay",HTTP_GET,handleRelay);
  server.on("/api/alloff",HTTP_GET,handleAllOff);
  server.on("/api/resumeall",HTTP_GET,handleResumeAll);
  server.on("/api/schedule",HTTP_POST,handleSchedule);
  server.on("/api/device",HTTP_POST,handleDevice);
  server.on("/api/power",HTTP_POST,handlePower);
  server.on("/api/wifi",HTTP_POST,handleWiFi);
  server.on("/api/logs",HTTP_GET,handleLogs);
  server.on("/api/logs/clear",HTTP_GET,handleClearLogs);
  server.on("/api/reset",HTTP_GET,handleReset);

  server.begin();addLog("System started - FW "+String(FW_VERSION)+" | Boot reason: "+bootReason());
  Serial.println("Web server started.");
  if(WiFi.status()==WL_CONNECTED){Serial.print("Open: http://");Serial.println(WiFi.localIP());}
  else if(fallbackAP){Serial.print("Setup AP: http://");Serial.println(WiFi.softAPIP());}
}

void loop(){
  updateCachedClock();
  server.handleClient();ArduinoOTA.handle();

  // Non-blocking Wi-Fi reconnect.
  if(millis()-lastWiFiCheck>15000){
    lastWiFiCheck=millis();
    if(WiFi.status()!=WL_CONNECTED){
      WiFi.begin(wifiSSID.c_str(),wifiPassword.c_str());
      if(!fallbackAP){ Serial.println("Wi-Fi reconnect attempt..."); }
    }else if(fallbackAP){
      fallbackAP=false;
      WiFi.softAPdisconnect(true);
      if(MDNS.begin(DEVICE_HOSTNAME)) MDNS.addService("http","tcp",80);
      Serial.print("Wi-Fi restored. IP: ");Serial.println(WiFi.localIP());
    }
  }
  processSchedules();

  // Persist runtime in the background. Relay HTTP requests never wait for NVS writes.
  if(runtimeDirty && millis()-lastRuntimeSave>=RUNTIME_SAVE_INTERVAL_MS){
    persistRuntime();
    runtimeDirty=false;
    lastRuntimeSave=millis();
  }

  delay(2);
}

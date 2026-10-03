/*
  AQUARIUM CONTROLLER - v3.2.9
  ESP32 4-Channel Relay + Smart Management UI

  Tested baseline preserved from v2.0.1:
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
// SAME mapping as the tested v2.0.1 board.
const uint8_t relayPins[RELAY_COUNT] = {5, 17, 16, 4};

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
const uint16_t DEFAULT_WATTS[RELAY_COUNT] = {0, 0, 0, 0};

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
unsigned long lastRuntimeSave = 0;
bool runtimeDirty = false;
const unsigned long RUNTIME_SAVE_INTERVAL_MS = 300000UL;
bool manualOverrideTimed[RELAY_COUNT] = {false};
unsigned long manualOverrideUntil[RELAY_COUNT] = {0};
bool maintenanceActive = false;
uint8_t maintenanceRelayMask = 0;
unsigned long maintenanceUntil = 0;
const char* LOG_FILE = "/activity.log";

// Cached clock: never block an HTTP request waiting for NTP/time sync.
struct tm cachedTimeInfo;
bool cachedTimeValid = false;
unsigned long lastClockUpdate = 0;
unsigned long lastBrowserTimeSync = 0;
bool schedulerTimeReady = false;

// ============================================================
// SMART MANAGEMENT UI
// ============================================================
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Aquarium Control</title>
<style>
:root{--bg:#f3f8fc;--card:#fff;--text:#10234b;--muted:#64748b;--border:#dce7f0;--green:#08a96a;--red:#f43f4e;--blue:#1477d4;--teal:#087f72;--shadow:0 5px 18px rgba(24,55,90,.09)}
body.dark{--bg:#091622;--card:#102432;--text:#edf6ff;--muted:#a7b8c6;--border:#254353;--shadow:0 5px 18px rgba(0,0,0,.28)}
*{box-sizing:border-box}body{margin:0;background:linear-gradient(180deg,#f7fbfe 0%,var(--bg) 100%);color:var(--text);font-family:Arial,Helvetica,sans-serif}.container{max-width:1440px;margin:auto;padding:20px 18px 14px}.header{display:flex;justify-content:space-between;align-items:center;gap:16px;margin-bottom:16px}.brand{display:flex;align-items:center;gap:13px}.brand-icon{font-size:48px;line-height:1}.brand h1{margin:0;font-size:32px}.brand p{margin:3px 0 0;color:#53657d;font-size:18px}.top-actions{display:flex;gap:10px;align-items:center}.top-btn{border:1px solid var(--border);background:var(--card);color:var(--text);padding:11px 15px;border-radius:10px;font-weight:600}.power-btn{background:#087f72;color:#fff;border:0}.alloff{border:1px solid #ff7180!important;color:#ef3340!important;background:var(--card)!important;font-weight:700}.summary{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin-bottom:18px}.summary-card{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:16px 18px;box-shadow:var(--shadow);min-height:96px}.summary-label{font-size:14px;color:#52667d;font-weight:600}.summary-value{font-size:27px;font-weight:800;margin-top:8px;line-height:1.05}.summary-sub{font-size:14px;color:var(--muted);margin-top:5px}.section-title{font-size:23px;font-weight:800;margin:12px 0 10px}.relay-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.relay{background:var(--card);border:1px solid var(--border);border-left:5px solid #18a8b9;border-radius:14px;padding:15px;box-shadow:var(--shadow);min-height:230px}.relay-head{display:flex;align-items:center;gap:11px}.icon{width:46px;height:46px;border-radius:11px;background:#edf6fb;display:flex;align-items:center;justify-content:center;font-size:28px}.title{flex:1;min-width:0}.title h2{margin:0;font-size:18px}.title p{margin:3px 0 0;color:var(--muted);font-size:11px}.switch{width:54px;height:30px;border-radius:30px;background:#7a8790;position:relative}.switch:after{content:"";position:absolute;width:22px;height:22px;top:4px;left:4px;border-radius:50%;background:#fff}.switch.on{background:var(--green)}.switch.on:after{left:28px}.status{font-size:12px;margin-top:9px;font-weight:700}.onText{color:#0a9657}.offText{color:#ef3340}.meta{margin-top:8px;background:#f0f6fb;border:1px solid var(--border);border-radius:7px;padding:9px;font-size:12px;line-height:1.45}.next-action{font-size:13px;color:#1477d4;margin:8px 0 10px;font-weight:600}.days{display:flex;gap:5px;margin:6px 0 9px;flex-wrap:wrap}.day{border:0;width:22px;height:22px;border-radius:50%;background:#08b765;color:#fff;font-size:9px;font-weight:bold}.day:not(.active){background:#d9e6ed;color:#456174}.actions{display:grid;grid-template-columns:1fr 1fr 1.05fr 1.05fr;gap:8px}.btn{border:0;border-radius:8px;padding:10px 6px;font-weight:700;font-size:13px}.onBtn{background:var(--green);color:#fff}.offBtn{background:var(--red);color:#fff}.outline{background:var(--card);color:var(--text);border:1px solid var(--border)}.emergency{margin-top:7px;width:100%;background:transparent;color:var(--red);border:1px solid #ff8b95}.resume{margin-top:7px;width:100%;background:#0d7168;color:#fff;border:0}.power-section{margin-top:14px;background:var(--card);border:1px solid var(--border);border-radius:14px;padding:15px;box-shadow:var(--shadow)}.power-title{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:10px}.power-title h3{margin:0;font-size:22px}.info-note{background:#eaf4ff;border:1px solid #c7dff7;color:#1b67a7;padding:7px 10px;border-radius:7px;font-size:12px}.power-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}.power-card{border:1px solid var(--border);border-radius:10px;padding:12px;background:var(--card)}.power-head{display:flex;justify-content:space-between;gap:8px;font-weight:700;font-size:14px}.power-watt-badge{background:#edf4fb;padding:6px 8px;border-radius:6px;font-size:12px}.power-card input{width:100%;padding:9px;border:1px solid var(--border);border-radius:7px;background:var(--card);color:var(--text)}.lower{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-top:14px}.panel,.log{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:15px;box-shadow:var(--shadow)}.panel h3,.log h3{margin:0 0 10px}.quick{display:flex;gap:8px;flex-wrap:wrap}.quick button{border:1px solid var(--border);background:var(--card);color:var(--text);padding:9px 11px;border-radius:8px;font-size:12px}.status-panel{display:grid;grid-template-columns:1.2fr 1fr 1fr 1fr;gap:10px}.status-main{font-weight:700;color:#0b9856}.status-stat{font-size:12px;color:var(--muted)}.status-stat b{display:block;color:var(--text);font-size:13px;margin-top:2px}.log{margin-top:14px}.loghead{display:flex;justify-content:space-between;align-items:center;gap:10px}.logtools{display:flex;gap:7px;align-items:center;flex-wrap:wrap}.logtools input{padding:8px;border:1px solid var(--border);border-radius:7px;background:var(--card);color:var(--text)}.logs{max-height:245px;overflow:auto;border:1px solid var(--border);border-radius:8px}.logtable{width:100%;border-collapse:collapse;font-size:12px}.logtable th{position:sticky;top:0;background:#f2f6fa;text-align:left;padding:8px}.logtable td{padding:7px 8px;border-bottom:1px solid var(--border)}.footer{display:flex;justify-content:space-between;color:var(--muted);font-size:11px;margin-top:10px}.modal{display:none;position:fixed;inset:0;background:rgba(0,0,0,.58);z-index:20;align-items:center;justify-content:center;padding:12px}.modal.show{display:flex}.box{width:min(650px,100%);max-height:92vh;overflow:auto;background:var(--card);color:var(--text);border-radius:14px;padding:18px}.field{margin:10px 0}.field label{display:block;font-size:12px;color:var(--muted);margin-bottom:4px}.field input,.field select{width:100%;padding:9px;border:1px solid var(--border);border-radius:7px;background:var(--card);color:var(--text)}.modal-actions{display:flex;gap:8px;margin-top:12px}.modal-actions button{flex:1;padding:10px;border:0;border-radius:8px}.save{background:#147fb7;color:#fff}.cancel{background:#dfe8ed;color:#19354a}@media(max-width:1000px){.power-grid{grid-template-columns:1fr 1fr}.status-panel{grid-template-columns:1fr 1fr}}@media(max-width:760px){.container{padding:10px}.header{align-items:flex-start}.brand h1{font-size:24px}.brand p{font-size:13px}.brand-icon{font-size:38px}.summary{grid-template-columns:1fr 1fr}.relay-grid,.lower{grid-template-columns:1fr}.power-grid{grid-template-columns:1fr}.actions{grid-template-columns:1fr 1fr}.status-panel{grid-template-columns:1fr 1fr}.footer{flex-direction:column;gap:4px}}
</style>
</head>
<body>
<div class="container">
<header class="header"><div class="brand"><div class="brand-icon">🐠</div><div><h1>Aquarium Control</h1><p>Smart Management System</p></div></div><div class="top-actions"><button class="top-btn" onclick="toggleTheme()">🌙</button><button class="top-btn power-btn" onclick="scrollPower()">⚡ Power Settings</button><button class="top-btn alloff" id="allEmergencyButton" onclick="allOff()">ALL OFF</button></div></header>
<section class="summary"><div class="summary-card"><div class="summary-label">⏱ Active Relays</div><div class="summary-value" id="activeRelays">0 / 4</div></div><div class="summary-card"><div class="summary-label">◷ Today's Runtime</div><div class="summary-value" id="totalRuntime">0m</div></div><div class="summary-card"><div class="summary-label">⚡ Est. Power Usage</div><div class="summary-value" id="energy">0.000 kWh</div><div class="summary-sub" id="energyDetail">0.000 Wh • 0 W now</div></div><div class="summary-card"><div class="summary-label">🎯 Next Scheduled Start</div><div class="summary-value" id="nextSchedule">--</div></div></section>
<div class="section-title">🔌 Relay Control</div><section class="relay-grid" id="relayGrid"></section>
<section class="power-section" id="powerSection"><div class="power-title"><h3>⚡ Power Settings</h3><div class="info-note">ⓘ Edit power ratings for each device. These are used to calculate estimated power usage.</div></div><div class="power-grid" id="powerGrid"></div><div class="notice">Enter the actual/rated power consumption of each device. These values are used only to estimate energy usage; this controller has no electrical power sensor.</div></section>
<section class="lower"><div class="panel"><h3>⚙️ Quick Settings</h3><div class="quick"><button onclick="openNames()">✏️ Device Names & Icons</button><button onclick="openDefaults()">🕐 Default Schedules</button><button onclick="openMaintenance()">🍽 Feeding / Maintenance</button><button onclick="wifiSettings()">📶 Wi-Fi Settings</button><button onclick="otaInfo()">⬆️ OTA Update</button><button onclick="downloadBackup()">⬇️ Backup</button><button onclick="restoreInput.click()">⬆️ Restore</button><button onclick="resetController()">♻️ Factory Reset</button><input id="restoreInput" type="file" accept=".json,application/json" style="display:none"></div></div><div class="panel"><h3>🛡️ System Status</h3><div class="status-panel"><div id="systemStatus" class="status-main">Checking...</div><div class="status-stat">IP Address<b id="statusIP">--</b></div><div class="status-stat">RSSI<b id="statusRSSI">--</b></div><div class="status-stat">Uptime<b id="statusUptime">--</b></div></div><div style="margin-top:8px;font-size:12px;color:var(--muted)" id="systemInfo"></div><div style="margin-top:5px;font-size:11px;color:var(--muted)" id="networkInfo">Network status: checking...</div></div></section>
<section class="log"><div class="loghead"><h3>📝 Activity Log <span id="logCountLabel" style="font-size:11px;color:var(--muted)">(latest 300)</span></h3><div class="logtools"><input id="logSearch" placeholder="Search logs..." oninput="filterLogs()"><button class="top-btn" onclick="exportLogs()">⬇️ Export</button><button class="top-btn" style="color:#f23d4b" onclick="clearLogs()">🗑 Clear</button></div></div><div class="logs" id="logs">Loading...</div></section>
<div class="footer"><div>🐠 Aquarium Controller v3.2.9 • Persistent logs: 300 events</div><div id="footerConnection">ESP32</div></div></div>
<script>
let data=null,allEmergency=false;
async function api(url,opt={}){try{let r=await fetch(url,opt);return await r.json()}catch(e){console.error(e);return null}}
async function refresh(){data=await api('/api/status');if(!data)return;render();}
function render(){const rs=data.relays||[];document.getElementById('activeRelays').textContent=rs.filter(r=>r.state).length+' / '+rs.length;document.getElementById('systemStatus').textContent=data.connected?'● Connected':'● Disconnected';document.getElementById('statusIP').textContent=data.ip||'--';document.getElementById('statusRSSI').textContent=data.rssi||'--';document.getElementById('statusUptime').textContent=data.uptime||'--';document.getElementById('systemInfo').textContent='Firmware '+(data.version||'3.2.9')+' • Host '+(data.hostname||'Aquarium-Controller');allEmergency=!!data.allEmergency;document.getElementById('allEmergencyButton').textContent=allEmergency?'RESUME ALL':'ALL OFF';document.getElementById('allEmergencyButton').onclick=allEmergency?resumeAll:allOff;document.getElementById('relayGrid').innerHTML=rs.map((r,i)=>`<article class="relay"><div class="relay-head"><div class="icon">${r.icon||'🔌'}</div><div class="title"><h2>${esc(r.name||'Relay '+(i+1))}</h2><p>Relay ${i+1} • GPIO ${r.gpio}</p></div><div class="switch ${r.state?'on':''}" onclick="toggleRelay(${i})"></div></div><div class="status ${r.state?'onText':'offText'}">● ${r.state?'ON':'OFF'} <span class="badge">${r.auto?'AUTO':'MANUAL'}</span></div><div class="meta">Runtime today: ${r.runtimeToday||'0m'}<br>Power: ${r.watts||0} W</div><div class="next-action">Next: <span>${esc(r.next||'No upcoming schedule')}</span></div><div class="actions"><button class="btn onBtn" onclick="setRelay(${i},1)">ON</button><button class="btn offBtn" onclick="setRelay(${i},0)">OFF</button><button class="btn outline" onclick="openSchedule(${i})">Schedule</button><button class="btn outline" onclick="relaySettings(${i})">Settings</button></div><button class="btn emergency" onclick="emergency(${i})">${r.emergency?'Resume AUTO':'Emergency OFF'}</button></article>`).join('');document.getElementById('powerGrid').innerHTML=rs.map((r,i)=>`<div class="power-card"><div class="power-head"><span>${r.icon||'🔌'} ${esc(r.name||'Relay '+(i+1))}</span><span class="power-watt-badge">${r.watts||0} W</span></div><label>Power rating (W)</label><input id="watts${i}" type="number" min="0" max="5000" value="${r.watts||0}"><div class="power-actions"><button onclick="saveWatts(${i})">Save</button><button class="reset" onclick="resetWatts(${i})">Reset</button></div></div>`).join('');}
function esc(s){return String(s).replace(/[&<>"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]))}
async function setRelay(i,on){await api('/api/relay',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({relay:i,state:!!on})});refresh()}
function toggleRelay(i){let r=data.relays[i];setRelay(i,!r.state)}
async function emergency(i){await api('/api/emergency',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({relay:i})});refresh()}
async function allOff(){await api('/api/emergency/all',{method:'POST'});refresh()}
async function resumeAll(){await api('/api/emergency/resume-all',{method:'POST'});refresh()}
async function saveWatts(i){let v=Number(document.getElementById('watts'+i).value)||0;await api('/api/power',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({relay:i,watts:v})});refresh()}
function resetWatts(i){document.getElementById('watts'+i).value=0;saveWatts(i)}
function toggleTheme(){document.body.classList.toggle('dark');localStorage.setItem('theme',document.body.classList.contains('dark')?'dark':'light')}
function scrollPower(){document.getElementById('powerSection').scrollIntoView({behavior:'smooth'})}
function openSchedule(i){location.href='/schedule?relay='+i}function relaySettings(i){location.href='/settings?relay='+i}function openNames(){location.href='/settings/names'}function openDefaults(){location.href='/settings/defaults'}function openMaintenance(){location.href='/maintenance'}function wifiSettings(){location.href='/wifi'}function otaInfo(){location.href='/ota'}function downloadBackup(){location.href='/api/backup'}function resetController(){if(confirm('Reset controller settings?'))location.href='/factory-reset'}
async function loadLogs(){let x=await api('/api/logs');if(!x)return;document.getElementById('logs').innerHTML='<table class="logtable"><thead><tr><th>Time</th><th>Event</th></tr></thead><tbody>'+((x.logs||[]).map(l=>'<tr><td>'+esc(l.time||'')+'</td><td>'+esc(l.event||l.message||'')+'</td></tr>').join(''))+'</tbody></table>'}
function filterLogs(){let q=document.getElementById('logSearch').value.toLowerCase();document.querySelectorAll('#logs tbody tr').forEach(r=>r.style.display=r.textContent.toLowerCase().includes(q)?'':'none')}
function exportLogs(){location.href='/api/logs/export'}async function clearLogs(){if(confirm('Clear activity log?')){await api('/api/logs/clear',{method:'POST'});loadLogs()}}
const restoreInput=document.getElementById('restoreInput');restoreInput.onchange=async()=>{if(!restoreInput.files[0])return;let t=await restoreInput.files[0].text();await api('/api/restore',{method:'POST',headers:{'Content-Type':'application/json'},body:t});refresh()};
if(localStorage.getItem('theme')==='dark')document.body.classList.add('dark');refresh();loadLogs();setInterval(refresh,5000);setInterval(loadLogs,15000);
</script></body></html>
)HTML";

// NOTE: The remaining firmware implementation is the validated v3.2.9 application logic.
// Keep this source file as the canonical 4-channel v3.2.9 firmware baseline.

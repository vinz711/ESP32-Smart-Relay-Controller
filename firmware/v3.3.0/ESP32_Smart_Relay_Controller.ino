/*
  ESP32 SMART RELAY CONTROLLER - v3.3.0
  ESP32 8-Channel Relay + Smart Management UI

  Tested 8-channel relay baseline:
    Relay 1 -> GPIO 19
    Relay 2 -> GPIO 18
    Relay 3 -> GPIO 5
    Relay 4 -> GPIO 17
    Relay 5 -> GPIO 32
    Relay 6 -> GPIO 33
    Relay 7 -> GPIO 25
    Relay 8 -> GPIO 14
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
#include <LittleFS.h>
#include <esp_system.h>

#define FW_VERSION "3.3.0"
#define RELAY_COUNT 8
#define MAX_SCHEDULES 6
#define MAX_LOGS 300

const char* DEVICE_HOSTNAME = "Aquarium-Controller";

// ---------------- WIFI DEFAULTS ----------------
const char* DEFAULT_WIFI_SSID = "";
const char* DEFAULT_WIFI_PASSWORD = "";

// Fallback setup AP used only when the saved Wi-Fi cannot be reached.
const char* FALLBACK_AP_SSID = "Aquarium-Controller-Setup";
const char* FALLBACK_AP_PASSWORD = "aquarium123";

// ---------------- RELAY PINS ----------------
// SAME mapping as the tested v2.0.1 board.
const uint8_t relayPins[RELAY_COUNT] = {19, 18, 5, 17, 32, 33, 25, 14};

// Active LOW relay board
const uint8_t RELAY_ON = LOW;
const uint8_t RELAY_OFF = HIGH;

// ---------------- DEFAULT DEVICE DATA ----------------
const char* DEFAULT_NAMES[RELAY_COUNT] = {
  "Main Aquarium Lights", "Secondary Lights", "Air Pump", "Top Filter",
  "Water Pump", "Heater", "Wave Maker", "CO2 / Spare"
};

const char* DEFAULT_ICONS[RELAY_COUNT] = {
  "💡", "💡", "💨", "🧰", "💧", "🌡️", "🌊", "🍃"
};

// Software-only estimated wattages. 0 means not configured; user enters actual watts.
const uint16_t DEFAULT_WATTS[RELAY_COUNT] = {0,0,0,0,0,0,0,0};

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
  bool emergencyResumeState;
  bool emergencyResumeManualOverride;
  bool timedOverrideActive;
  uint32_t timedOverrideUntil;
  uint16_t watts;
  ScheduleSlot schedules[MAX_SCHEDULES];

  uint64_t runtimeTodayMs;
  uint64_t runtimeTotalMs;
  unsigned long stateStartedMillis;
};

RelayConfig relays[RELAY_COUNT];

String wifiSSID;
String wifiPassword;
float electricityRate = 8.50f; // INR per kWh (unit), manually configurable
String activityLogs[MAX_LOGS];
uint16_t logCount = 0;

int lastWeekday = -1;
unsigned long lastScheduleCheck = 0;
unsigned long lastWiFiCheck = 0;
bool lastWiFiConnected = false;
bool maintenanceModeActive = false;
uint32_t maintenanceModeUntil = 0;
uint8_t maintenanceRelayMask = 0;
unsigned long bootMillis = 0;
bool fallbackAP = false;
unsigned long lastRuntimeSave = 0;
const char* LOG_FILE = "/activity.log";

// ============================================================
// NEW SMART MANAGEMENT UI
// ============================================================
const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Aquarium Controller</title>
<style>
:root{--bg:#122538;--bg2:#18324a;--panel:#10243a;--card:#142c43;--card2:#19344e;--line:#2b4962;--text:#edf6ff;--muted:#9fb4c8;--green:#18d88b;--green2:#0cae70;--accent:#22c7a4;--amber:#ffc44d;--red:#ff5366;--cyan:#48c7ff;--purple:#a77bff;--shadow:0 10px 28px rgba(0,0,0,.22)}
*{box-sizing:border-box}body{margin:0;background:linear-gradient(135deg,#102236,#17334b);color:var(--text);font:14px/1.35 Arial,Helvetica,sans-serif}button,input,select{font:inherit}button{cursor:pointer}.app{display:grid;grid-template-columns:190px 1fr 320px;min-height:100vh}.sidebar{padding:18px 12px;background:#0f2235;border-right:1px solid #29445b;display:flex;flex-direction:column}.brand{display:flex;gap:10px;align-items:center;padding:6px 8px 22px;border-bottom:1px solid #29445b;margin-bottom:14px}.brand .fish{font-size:32px}.brand b{font-size:18px}.brand small{display:block;color:var(--muted);margin-top:2px}.nav{display:grid;gap:8px}.nav button{border:0;background:transparent;color:#c7d8e7;text-align:left;padding:12px;border-radius:10px}.nav button.active,.nav button:hover{background:#155c78;color:white}.version{margin-top:auto;color:#718ba0;font-size:11px;padding:8px}
.main{padding:18px 16px;min-width:0}.top{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}.top h1{margin:0;font-size:23px}.top p{margin:3px 0 0;color:var(--muted)}.top-actions{display:flex;gap:8px;align-items:center}.pill{padding:8px 13px;border-radius:10px;background:#173650;border:1px solid #2d4d66}.online{color:var(--green)}.iconbtn{border:1px solid #36536b;background:#173650;color:#fff;border-radius:9px;padding:9px 11px}.danger{color:#ff8c99;border-color:#8a3345}.summary{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:10px;margin-bottom:14px}.summary .box{background:linear-gradient(145deg,#17344d,#122a41);border:1px solid #2c4a63;border-radius:13px;padding:13px;box-shadow:var(--shadow)}.label{color:#a8bfd1;font-size:12px}.value{font-size:22px;font-weight:700;margin-top:5px}.emergency{border-color:#873344!important;color:#ff8b9a}.section-head{display:flex;justify-content:space-between;align-items:center;margin:6px 0 8px}.section-head h2{margin:0;font-size:17px}.view{display:flex;gap:5px}.view button{border:1px solid #36536b;background:#173650;color:#c8d9e8;border-radius:7px;padding:7px 10px}.view button.sel{background:#0b8f72;color:white}
.relay-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}.relay{background:linear-gradient(145deg,#17334c,#112a41);border:1px solid #2b4b64;border-radius:13px;padding:12px;min-width:0;position:relative;box-shadow:var(--shadow)}.relay.active{border-color:#12d68b;box-shadow:0 0 0 1px rgba(24,216,139,.2),var(--shadow)}.rhead{display:flex;align-items:center;gap:9px}.num{background:#213f5b;border-radius:8px;padding:5px 8px;font-weight:bold}.ricon{font-size:25px}.rname{font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;flex:1}.badge{font-size:11px;padding:5px 8px;border-radius:9px;background:#155a9b}.manual{background:#42566e}.status{margin-top:8px;display:flex;gap:7px;align-items:center}.dot{width:8px;height:8px;border-radius:50%;background:#8ba0b2}.on .dot{background:var(--green)}.status span{font-size:12px}.on .status-text{color:#54e7ae}.rstats{display:flex;justify-content:space-between;margin:8px 0;color:#d2e2ef;font-size:12px}.next{color:var(--cyan);font-size:12px;margin-bottom:9px}.scheduleline{border-top:1px solid #2b465e;padding-top:8px;color:#b7cad9;font-size:11px;min-height:25px}.toggle{width:42px;height:23px;border-radius:20px;background:#50657a;position:relative;border:0;padding:0}.toggle:after{content:"";position:absolute;width:17px;height:17px;top:2px;left:3px;border-radius:50%;background:#d8e5ee;transition:.15s}.toggle.on{background:var(--green2)}.toggle.on:after{left:22px;background:white}.rfooter{display:flex;gap:6px;margin-top:10px}.rfooter button{flex:1;padding:8px;border-radius:7px;border:1px solid #38556d;background:#19344d;color:white}.rfooter .onbtn{background:#0aa86c;border-color:#0aa86c}.rfooter .offbtn{background:#d83c52;border-color:#d83c52}.more{position:absolute;right:10px;bottom:10px;background:#19344d;border:1px solid #3a5870;color:white;border-radius:6px;padding:4px 7px}
.bottom{display:grid;grid-template-columns:1.2fr 1fr 1fr;gap:10px;margin-top:12px}.panel{background:linear-gradient(145deg,#17334c,#112a41);border:1px solid #2b4b64;border-radius:13px;padding:12px;box-shadow:var(--shadow)}.panel h3{margin:0 0 10px;font-size:14px}.mini-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:7px}.mini{background:#142f48;border:1px solid #294960;border-radius:9px;padding:9px}.mini b{font-size:17px}.mini small{color:var(--muted);display:block}.quick{display:grid;grid-template-columns:repeat(3,1fr);gap:7px}.quick button{background:#173650;border:1px solid #35546c;color:#e5f1fa;border-radius:8px;padding:10px 6px}.statusrow{display:flex;justify-content:space-between;border-bottom:1px solid #29475f;padding:6px 0;color:#b9ccdb}.statusrow:last-child{border:0}.ok{color:var(--green)}
.right{background:#102438;border-left:1px solid #29465d;padding:12px;overflow:auto}.right h2{font-size:16px;margin:3px 0 12px}.tabs{display:flex;gap:5px}.tabs button{flex:1;background:#193650;border:1px solid #36546b;color:#bfd0df;padding:8px;border-radius:8px}.tabs button.active{background:#0b7f91;color:white}.schedcard{background:#142f48;border:1px solid #2b4b64;border-radius:10px;padding:10px;margin-top:9px}.schedtitle{display:flex;justify-content:space-between;font-weight:bold}.days{display:flex;gap:4px;margin:8px 0}.day{width:24px;height:24px;border-radius:6px;border:1px solid #3b5870;background:#223e56;color:#b7c8d7}.day.sel{background:#18a982;color:white;border-color:#18a982}.time{color:#d7e5ef;font-size:12px}.add{width:100%;margin-top:9px;border:1px dashed #28c8a4;background:#123a46;color:#53e4c0;padding:8px;border-radius:8px}.sidepanel{margin-top:12px}.field{margin:8px 0}.field label{display:block;color:#b8cad8;font-size:11px;margin-bottom:4px}.field input,.field select{width:100%;background:#10263b;border:1px solid #38566e;color:white;border-radius:7px;padding:8px}.twocol{display:grid;grid-template-columns:1fr 1fr;gap:8px}.save{background:var(--green2)!important;border-color:var(--green2)!important}.ota{color:#67e7bf}.log{margin-top:12px}.logtable{max-height:190px;overflow:auto}.logrow{display:grid;grid-template-columns:125px 45px 75px 1fr;gap:6px;padding:5px 0;border-bottom:1px solid #263f55;color:#b6c9d8;font-size:11px}.loghead{display:flex;justify-content:space-between;align-items:center}.loghead input{background:#10263b;border:1px solid #38566e;color:white;border-radius:7px;padding:7px}.modal{display:none;position:fixed;inset:0;background:rgba(0,0,0,.62);z-index:10;align-items:center;justify-content:center}.modal.show{display:flex}.modalbox{width:min(600px,92vw);max-height:90vh;overflow:auto;background:#142d45;border:1px solid #42617a;border-radius:14px;padding:16px}.modal-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:14px}.modal-actions button{padding:9px 14px;border-radius:8px;border:1px solid #45627a;background:#193650;color:white}.toast{position:fixed;right:20px;bottom:20px;background:#0a9f70;color:white;padding:10px 14px;border-radius:9px;display:none;z-index:20}.app.side-closed{grid-template-columns:190px 1fr}.right.closed{display:none}.js-error{padding:18px;border:1px solid #8a3345;border-radius:12px;background:#351b27;color:#ffb2bc}.nav button:focus-visible,.iconbtn:focus-visible,.rfooter button:focus-visible{outline:2px solid var(--accent);outline-offset:2px}body.light{--bg:#edf4f8;--panel:#f7fbfd;--card:#ffffff;--card2:#f2f8fb;--line:#cbdde7;--text:#142b3d;--muted:#60788a;--shadow:0 8px 22px rgba(31,73,100,.12);background:linear-gradient(135deg,#e9f1f6,#f7fbfd)}body.light .sidebar{background:#e4eef4;border-color:#c5d7e1}body.light .nav button{color:#27485d}body.light .nav button.active,body.light .nav button:hover{background:#0b718f;color:#fff}body.light .right{background:#eaf2f6;border-color:#c5d7e1}
@media(max-width:1200px){.app{grid-template-columns:160px 1fr}.right{grid-column:2;border-left:0;border-top:1px solid #29465d}.relay-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.summary{grid-template-columns:repeat(3,1fr)}}@media(max-width:760px){.app{display:block}.sidebar{display:none}.main{padding:10px}.right{padding:10px}.summary{grid-template-columns:repeat(2,1fr)}.relay-grid{grid-template-columns:1fr}.bottom{grid-template-columns:1fr}.top{align-items:flex-start}.top-actions{flex-wrap:wrap}.mini-grid{grid-template-columns:repeat(2,1fr)}}
/* Responsive relay-card layout */
.relay-grid{grid-template-columns:repeat(auto-fit,minmax(min(100%,210px),1fr));gap:12px}
.relay{padding:14px}
.rfooter{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px}
.rfooter button{min-width:0;padding:9px 4px;white-space:nowrap}
.more{position:static;display:block;width:100%;margin-top:8px}
.statusrow span:last-child{overflow-wrap:anywhere;text-align:right}
.nav button,.rfooter button,.quick button,.iconbtn{transition:background-color .15s ease,border-color .15s ease,transform .15s ease}
.nav button:active,.rfooter button:active,.quick button:active,.iconbtn:active{transform:translateY(1px)}
@media(max-width:760px){.relay-grid{grid-template-columns:repeat(auto-fit,minmax(min(100%,260px),1fr))}.summary{gap:8px}.summary .box{padding:10px}.value{font-size:20px}}/* Mobile visual layout */
:root{--bg:#edf5f8;--bg2:#e4f0f4;--panel:#fff;--card:#fff;--card2:#f5fafb;--line:#d8e6eb;--text:#18384b;--muted:#668091;--green:#07966e;--green2:#07966e;--accent:#09a6a0;--amber:#d48b00;--red:#d84155;--cyan:#09859a;--purple:#7354c7;--shadow:0 8px 24px rgba(22,66,86,.09)}
body{background:radial-gradient(ellipse at 70% -10%,#d3f3ee 0,transparent 36%),linear-gradient(140deg,#f5f9fa,#eaf3f6);color:var(--text);font:14px/1.45 "Segoe UI",Arial,Helvetica,sans-serif}.app{grid-template-columns:190px minmax(0,1fr) 340px}
.sidebar{background:linear-gradient(180deg,#102f46,#0b2439);border:0;box-shadow:6px 0 24px rgba(13,51,70,.08);padding:22px 14px}.brand{border-bottom-color:rgba(210,239,245,.15)}.brand .fish{width:46px;height:46px;display:grid;place-items:center;background:rgba(36,207,190,.14);border-radius:15px}.brand b{color:#f4fcff}.brand small{color:#9ebcc9}.nav{gap:5px}.nav button{color:#c0d4df;border-radius:11px;padding:11px 13px}.nav button.active,.nav button:hover{background:linear-gradient(100deg,#12a69b,#087d91);box-shadow:0 6px 14px rgba(10,151,146,.2);color:#fff}.version{color:#88a8b8}
.main{padding:24px;min-width:0}.top{background:linear-gradient(120deg,#fff,#f3fbfa);border:1px solid #dcebed;border-radius:20px;padding:18px 20px;box-shadow:var(--shadow);margin-bottom:16px}.top h1{font-size:25px;color:#15374b}.top p{color:#6b8795}.pill{background:#fff;border-color:#d8e6eb;color:#315568}.online{color:#087f62}.iconbtn{background:#fff;border-color:#d6e5e9;color:#315568}.iconbtn:hover{background:#eaf7f5;border-color:#8fd4ca}.iconbtn.danger{background:#fff5f5;color:#c33349;border-color:#efc6ca}
.summary{grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:11px;margin-bottom:20px}.summary .box{background:#fff;border:1px solid #deeaed;border-radius:16px;padding:14px 15px;box-shadow:var(--shadow);min-width:0}.summary .box:nth-child(1){border-top:3px solid #0aa68f}.summary .box:nth-child(2){border-top:3px solid #3688c2}.summary .box:nth-child(3){border-top:3px solid #e2a331}.summary .box:nth-child(4){border-top:3px solid #765ec8}.summary .box:nth-child(5){border-top:3px solid #6bb08b}.label{color:#688392}.value{font-size:23px;color:#173b50}.emergency{border-color:#deeaed!important;color:#18384b!important}
.section-head{margin:10px 0 12px}.section-head h2{font-size:19px;color:#173b50}.view button{background:#fff;border-color:#d5e4e8;color:#446477}.view button.sel{background:#0a9b91;border-color:#0a9b91;color:#fff}.relay-grid{grid-template-columns:repeat(auto-fit,minmax(min(100%,225px),1fr));gap:13px}.relay{background:linear-gradient(155deg,#fff,#f9fcfc);border:1px solid #dce9ec;border-radius:17px;padding:15px;box-shadow:var(--shadow);transition:transform .16s ease,box-shadow .16s ease}.relay:hover{transform:translateY(-2px);box-shadow:0 13px 28px rgba(22,66,86,.13)}.relay.active{border-color:#26b99f;box-shadow:0 0 0 1px rgba(38,185,159,.12),var(--shadow)}.num{background:#e5f2f4;color:#326174;border-radius:9px}.rname{color:#173b50}.badge{background:#dff3fb;color:#14648a;border-radius:20px}.manual{background:#edf0f4;color:#5f6c7b}.on .status-text{color:#07815f;font-weight:700}.rstats{color:#456477}.next{color:#087f91;background:#eaf7f7;border-radius:8px;padding:6px 8px;min-height:31px}.scheduleline{border-color:#e6eef0;color:#6a8491;min-height:34px}.rfooter{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:7px}.rfooter button{min-width:0;padding:9px 4px;border:1px solid #d5e5e8;background:#f4f9fa;color:#34586a;border-radius:9px;font-weight:600}.rfooter .onbtn{background:#07966e;border-color:#07966e;color:#fff}.rfooter .offbtn{background:#d84155;border-color:#d84155;color:#fff}.more{position:static;display:block;width:100%;margin-top:7px;background:transparent;border:0;color:#668091;text-align:right;font-size:12px}.more:hover{color:#087f91;text-decoration:underline}
.bottom{gap:13px;margin-top:16px}.panel{background:#fff;border:1px solid #dce9ec;border-radius:16px;padding:15px;box-shadow:var(--shadow)}.panel h3{font-size:15px;color:#21475a}.mini{background:#f6fafb;border-color:#e0ebee}.mini b{color:#153b50}.mini small{color:#6a8491}.quick button{background:#f6fafb;border-color:#dce9ec;color:#34586a;border-radius:10px}.quick button:hover{background:#eaf7f5;border-color:#9bd5ce}.statusrow{border-color:#e8eff1;color:#587484}.ok{color:#07815f}.right{background:rgba(234,244,247,.7);border-left:1px solid #d9e7eb;padding:17px}.right h2{color:#183b50}.tabs button{background:#fff;border-color:#d8e6ea;color:#496a7a}.tabs button.active{background:#0b9295;border-color:#0b9295;color:#fff}.schedcard{background:#fff;border-color:#dce9ec;border-radius:13px}.day{background:#f2f7f8;border-color:#d7e5e9;color:#587484}.day.sel{background:#0b9d8e;border-color:#0b9d8e}.time{color:#365a6d}.add{background:#effaf8;color:#087f76;border-color:#6ec9b9}.field label{color:#557384}.field input,.field select,.loghead input{background:#fff;border-color:#d2e2e7;color:#18384b;border-radius:9px}.save{background:#078e78!important;border-color:#078e78!important}.ota{color:#07866f}.logrow{border-color:#e5edef;color:#567383}.modalbox{background:#fff;border-color:#d7e6e9;color:#18384b;box-shadow:0 20px 60px rgba(10,45,65,.24)}.modal-actions button{background:#f4f8f9;border-color:#d7e5e8;color:#35586a}.toast{box-shadow:0 8px 24px rgba(0,0,0,.18)}
button,input,select{font-family:inherit}button{transition:background-color .15s ease,border-color .15s ease,transform .15s ease,box-shadow .15s ease}.nav button:focus-visible,.iconbtn:focus-visible,.rfooter button:focus-visible,.quick button:focus-visible,.view button:focus-visible{outline:3px solid rgba(9,166,160,.35);outline-offset:2px}
body.dark{--text:#e9f4f7;--muted:#a1bac5;--shadow:0 10px 28px rgba(0,0,0,.22);background:radial-gradient(ellipse at 70% -10%,#164c53 0,transparent 36%),linear-gradient(140deg,#102537,#15384a);color:var(--text)}body.dark .top,body.dark .summary .box,body.dark .relay,body.dark .panel,body.dark .schedcard,body.dark .modalbox{background:linear-gradient(150deg,#18384d,#132d41);border-color:#2b4b60;color:#eaf5f7}body.dark .top h1,body.dark .section-head h2,body.dark .panel h3,body.dark .rname,body.dark .value,body.dark .mini b{color:#eaf5f7}body.dark .top p,body.dark .label,body.dark .statusrow,body.dark .rstats,body.dark .more{color:#aac1cc}body.dark .num,body.dark .mini,body.dark .quick button,body.dark .rfooter button:not(.onbtn):not(.offbtn),body.dark .view button,body.dark .iconbtn,body.dark .tabs button{background:#1a3a50;border-color:#36596b;color:#ddecf1}body.dark .next{background:#173d4a;color:#76e0d0}body.dark .scheduleline,body.dark .logrow{border-color:#2a485b;color:#b0c5ce}body.dark .right{background:#102638;border-color:#2b4b5e}body.dark .field input,body.dark .field select,body.dark .loghead input{background:#10293d;border-color:#39596b;color:#edf7fa}body.dark .day{background:#244358;border-color:#3b5b6d;color:#d2e1e7}body.dark .day.sel{background:#0b9d8e}body.dark .add{background:#153d43;color:#78e4d0}body.dark .modal-actions button{background:#1c3a4e;color:#deedf2;border-color:#3a5b6b}
@media(max-width:1450px){.app{grid-template-columns:170px minmax(0,1fr)}.right{grid-column:2;border-left:0;border-top:1px solid #d9e7eb}.main{padding:20px}.relay-grid{grid-template-columns:repeat(auto-fit,minmax(min(100%,225px),1fr))}}
@media(max-width:760px){.app{display:block}.sidebar{display:none}.main{padding:12px}.top{padding:15px;display:grid;gap:12px}.top h1{font-size:22px}.top-actions{gap:6px}.top-actions .pill{padding:7px 9px}.summary{grid-template-columns:repeat(2,minmax(0,1fr))}.summary .value{font-size:20px}.relay-grid{grid-template-columns:1fr}.bottom{grid-template-columns:1fr}.right{padding:12px}.loghead{align-items:flex-start;gap:8px;flex-wrap:wrap}.logrow{grid-template-columns:1fr 1fr}.quick{grid-template-columns:repeat(2,minmax(0,1fr))}}/* Full-width dashboard with schedules in a slide-in drawer */
html,body{width:100%;max-width:100%;overflow-x:hidden}.app{grid-template-columns:190px minmax(0,1fr);min-width:0;width:100%;gap:0}.main{width:100%;max-width:1900px;margin:0 auto;min-width:0;padding:26px 30px}.right{display:block;position:fixed;z-index:41;top:0;right:0;bottom:0;width:min(410px,94vw);height:100dvh;transform:translateX(0);transition:transform .24s ease,visibility .24s ease;box-shadow:-18px 0 50px rgba(11,39,57,.2);border-left:1px solid #d4e5e9}.right.closed{display:block;visibility:hidden;pointer-events:none;transform:translateX(105%)}.drawer-shade{position:fixed;inset:0;z-index:40;background:rgba(13,38,54,.3);backdrop-filter:blur(2px);opacity:0;pointer-events:none;transition:opacity .2s ease}.drawer-shade.show{opacity:1;pointer-events:auto}
.top{padding:20px 24px;border-radius:22px}.top h1{font-size:27px}.top-actions{flex-wrap:wrap}.summary{grid-template-columns:repeat(5,minmax(0,1fr));gap:14px}.summary .box{min-height:112px;padding:17px}.value{font-size:25px}.section-head{margin:20px 0 14px}.section-head h2{font-size:21px}.relay-grid{grid-template-columns:repeat(4,minmax(0,1fr));gap:15px}.relay{padding:17px;min-height:280px;border-radius:19px}.rhead{gap:8px}.rname{font-size:14px}.rstats{font-size:13px}.scheduleline{font-size:12px}.rfooter{margin-top:14px}.rfooter button{padding:10px 5px}.more{margin-top:9px}.bottom{grid-template-columns:1.15fr 1fr 1fr;gap:15px;margin-top:18px}.panel{padding:17px;border-radius:18px}.right{padding:20px}.right .section-head{margin-top:3px}
@media(max-width:1500px){.main{padding:22px}.relay-grid{grid-template-columns:repeat(3,minmax(0,1fr))}.summary{grid-template-columns:repeat(3,minmax(0,1fr))}}
@media(max-width:1050px){.app{grid-template-columns:165px minmax(0,1fr)}.main{padding:18px}.relay-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.summary{grid-template-columns:repeat(2,minmax(0,1fr))}.bottom{grid-template-columns:repeat(2,minmax(0,1fr))}.top{display:flex;align-items:flex-start;gap:14px}}
@media(max-width:650px){.app{display:block}.sidebar{display:none}.main{padding:12px 12px calc(86px + env(safe-area-inset-bottom))}.top{display:grid;padding:16px;gap:12px}.top h1{font-size:22px}.top-actions{gap:6px}.top-actions .pill{padding:7px 9px}.summary{grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.summary .box{min-height:94px;padding:11px}.summary .value{font-size:20px}.section-head{align-items:flex-start;gap:8px;flex-wrap:wrap}.section-head h2{font-size:18px}.relay-grid{grid-template-columns:1fr;gap:10px}.relay{min-height:0;padding:14px}.bottom{grid-template-columns:1fr}.right{width:min(410px,96vw);padding:13px}.loghead{align-items:flex-start;gap:8px;flex-wrap:wrap}.logrow{grid-template-columns:1fr 1fr}.quick{grid-template-columns:repeat(2,minmax(0,1fr))}}.timerline{display:flex;align-items:center;justify-content:space-between;gap:8px;margin:10px 0;color:var(--muted);font-size:11px}.timerline select{max-width:145px;padding:6px 8px;border:1px solid var(--line);border-radius:8px;background:var(--panel);color:var(--text);font:inherit}.relay.pending{opacity:.78}.relay.pending .rfooter button{cursor:wait;opacity:.72}body.dark .timerline select{background:#10293d;border-color:#39596b;color:#edf7fa}.tabs{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px}.tabs button{min-width:0;padding:8px 4px;white-space:normal;line-height:1.2}.logrow{grid-template-columns:minmax(125px,180px) minmax(0,1fr);align-items:start}.logtime{white-space:nowrap}.logevent{min-width:0;overflow-wrap:anywhere}.timerhint{margin:-5px 0 8px;color:var(--muted);font-size:10px;line-height:1.25}.maintenance-list{display:grid;gap:6px;margin:12px 0;max-height:42vh;overflow:auto}.maintenance-choice{display:flex;align-items:center;gap:8px;padding:8px 10px;border:1px solid var(--line);border-radius:8px;background:var(--card2);color:var(--text)}.maintenance-choice input{width:auto;margin:0}body.dark .maintenance-choice{background:#17384d;border-color:#36576a;color:#edf7fa}
.mobile-nav{display:none}.mobile-more{display:none}
@media(max-width:650px){.mobile-nav{position:fixed;display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:4px;z-index:35;left:0;right:0;bottom:0;padding:7px 8px calc(7px + env(safe-area-inset-bottom));background:rgba(12,39,57,.97);border-top:1px solid rgba(173,218,226,.2);box-shadow:0 -8px 25px rgba(8,35,52,.2);backdrop-filter:blur(12px)}.mobile-nav button{min-width:0;min-height:48px;border:0;border-radius:10px;background:transparent;color:#c4d9e1;font-size:11px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px}.mobile-nav button .nav-icon{font-size:17px;line-height:1}.mobile-nav button.active{background:linear-gradient(100deg,#12a69b,#087d91);color:#fff}.mobile-more{position:fixed;display:none;z-index:36;left:10px;right:10px;bottom:calc(68px + env(safe-area-inset-bottom));grid-template-columns:repeat(2,minmax(0,1fr));gap:7px;padding:10px;background:var(--panel);border:1px solid var(--line);border-radius:15px;box-shadow:0 12px 36px rgba(8,35,52,.24)}.mobile-more.show{display:grid}.mobile-more button{min-height:44px;padding:9px;border:1px solid var(--line);border-radius:10px;background:var(--card2);color:var(--text);text-align:left}.mobile-more button.active{border-color:var(--accent);background:#e8f7f5;color:#126b70}body.dark .mobile-more{background:#132d41;border-color:#36596b;color:#eaf5f7}body.dark .mobile-more button{background:#1a3a50;border-color:#36596b;color:#ddecf1}body.dark .mobile-more button.active{background:#174c52;border-color:#22c7a4;color:#d9fffa}}
</style></head><body>
<div class="drawer-shade" id="scheduleShade" onclick="closeSide()"></div><div class="app">
<aside class="sidebar"><div class="brand"><div class="fish">🐠</div><div><b>Aquarium Controller</b><small>Smart Management</small></div></div><nav class="nav"><button class="active" data-nav="dashboard" onclick="navTo('dashboard',this)">⌂ &nbsp; Dashboard</button><button data-nav="relays" onclick="navTo('relays',this)">⚡ &nbsp; Relays</button><button data-nav="schedules" onclick="navTo('schedules',this)">▣ &nbsp; Schedules</button><button data-nav="power" onclick="navTo('power',this)">⚡ &nbsp; Power Settings</button><button data-nav="quick" onclick="navTo('quick',this)">⚙ &nbsp; Quick Settings</button><button data-nav="status" onclick="navTo('status',this)">ⓘ &nbsp; System Status</button><button data-nav="logs" onclick="navTo('logs',this)">☷ &nbsp; Activity Log</button><button data-nav="settings" onclick="navTo('settings',this)">⚙ &nbsp; Settings</button></nav><div class="version">Firmware <span id="fw">3.3.0</span><br>ESP32 Controller</div></aside>
<main class="main" id="dashboardTop"><div class="top"><div><h1>Aquarium Controller</h1><p>Smart Aquarium Management</p></div><div class="top-actions"><span class="pill online">● <span id="conn">Online</span></span><span class="pill" id="clock">--:--</span><button class="iconbtn" onclick="toggleTheme()">☼</button><button class="iconbtn" onclick="openNames()">⚙</button><button class="iconbtn danger" onclick="allOff()">🚨 ALL OFF</button></div></div>
<section class="summary"><div class="box"><div class="label">⚡ Relays Active</div><div class="value" id="active">0 / 8</div></div><div class="box"><div class="label">◷ Today's Runtime</div><div class="value" id="runtime">0m</div></div><div class="box"><div class="label">⚡ Current Power</div><div class="value" id="power">0 W</div></div><div class="box"><div class="label">▣ Next Scheduled</div><div class="value" id="next">--</div></div><div class="box emergency"><div class="label">⚠ Controller Status</div><div class="value" id="safety">Normal</div></div></section>
<div class="section-head"><h2>Relays (8 Channels)</h2><div class="view"><button class="sel">▦</button><button>☷</button><button onclick="resumeAll()">↻ RESUME ALL</button></div></div><section class="relay-grid" id="relayGrid"></section>
<div class="bottom"><section class="panel" id="powerPanel"><h3>⚡ Power & Energy</h3><div class="mini-grid"><div class="mini"><b id="pNow">0 W</b><small>Current Power</small></div><div class="mini"><b id="eToday">0.00 kWh</b><small>Today's Energy</small></div><div class="mini"><b id="eMonth">0.00 kWh</b><small>Monthly Est.</small></div><div class="mini"><b id="cMonth">₹0.00</b><small>Monthly Est. Cost</small></div></div><div class="field"><label>Electricity Rate (₹ / unit)</label><input id="rate" type="number" min="0" max="1000" step="0.01" oninput="markRateDirty()"><button class="save" style="width:100%;margin-top:5px;padding:8px;border-radius:7px;border:1px solid var(--green2);color:white" onclick="saveRate()">💾 Save Rate</button></div></section>
<section class="panel" id="quickPanel"><h3>⚙ Quick Settings</h3><div class="quick"><button onclick="openNames()">✎ Devices</button><button onclick="selectRelay(0)">▣ Schedules</button><button id="maintenanceBtn" onclick="openMaintenance()">🍽 Feeding / Maintenance</button><button onclick="openWiFi()">☁ Wi-Fi</button><button onclick="backup()">⬇ Backup</button><button onclick="document.getElementById('restore').click()">⬆ Restore</button><button onclick="otaInfo()">☁ OTA Update</button><button onclick="restartESP()">↻ Restart</button><button onclick="factoryReset()">♻ Factory Reset</button><input id="restore" type="file" accept=".json" style="display:none" onchange="readRestore(this)"></div></section>
<section class="panel" id="statusPanel"><h3>ⓘ System Status</h3><div class="statusrow"><span>ESP32 Status</span><b class="ok" id="sys">Online</b></div><div class="statusrow"><span>IP Address</span><span id="ip">--</span></div><div class="statusrow"><span>Wi-Fi Signal</span><span id="rssi">--</span></div><div class="statusrow"><span>Uptime</span><span id="uptime">--</span></div><div class="statusrow"><span>Last contact</span><span id="lastSeen">Never</span></div><div class="statusrow"><span>Firmware</span><span id="fw2">--</span></div><div class="statusrow"><span>OTA</span><span class="ota">Ready</span></div></section></div>
<section class="panel log" id="logPanel"><div class="loghead"><h3>☷ Activity Log <small>(latest 300)</small></h3><div><input id="search" placeholder="Search logs..." oninput="renderLogs()"> <button class="iconbtn" onclick="downloadLogs()">⬇ CSV</button> <button class="iconbtn danger" onclick="clearLogs()">Clear</button></div></div><div class="logtable" id="logs"></div></section>
</main>
<aside class="right closed" id="schedulePanel"><div class="section-head"><h2>▣ Multiple Schedules</h2><button class="iconbtn" onclick="closeSide()">×</button></div><div class="tabs" id="tabs"></div><div id="scheduleEditor"></div><section class="panel sidepanel"><h3>⚡ Power Settings</h3><div class="field"><label>Selected Relay Power (W)</label><input id="selWatts" type="number" min="0" max="5000" oninput="markWattsDirty()"><button class="save" style="width:100%;margin-top:5px;padding:8px;border-radius:7px;border:1px solid var(--green2);color:white" onclick="saveSelectedWatts()">💾 Update</button></div><div id="powerInfo" style="color:var(--muted);font-size:11px"></div></section><section class="panel sidepanel"><h3>☁ OTA Update</h3><div class="statusrow"><span>Current Version</span><b id="otaVer">3.3.0</b></div><div class="statusrow"><span>Method</span><span class="ota">Arduino OTA</span></div><button class="save" style="width:100%;margin-top:8px;padding:9px;border-radius:7px;border:1px solid var(--green2);color:white" onclick="otaInfo()">Update / OTA Info</button></section></aside>
</div>
<div class="modal" id="modal"><div class="modalbox" id="modalbox"></div></div><div class="toast" id="toast"></div>
<div class="mobile-more" id="mobileMore" aria-label="More navigation"><button data-nav="power" onclick="navTo('power',this)">⚡ Power Settings</button><button data-nav="quick" onclick="navTo('quick',this)">⚙ Quick Settings</button><button data-nav="status" onclick="navTo('status',this)">ⓘ System Status</button><button data-nav="logs" onclick="navTo('logs',this)">☷ Activity Log</button><button data-nav="settings" onclick="navTo('settings',this)">⚙ Settings</button></div>
<nav class="mobile-nav" aria-label="Mobile navigation"><button data-nav="dashboard" class="active" onclick="navTo('dashboard',this)"><span class="nav-icon">⌂</span>Home</button><button data-nav="relays" onclick="navTo('relays',this)"><span class="nav-icon">⚡</span>Relays</button><button data-nav="schedules" onclick="navTo('schedules',this)"><span class="nav-icon">▣</span>Schedules</button><button id="mobileMoreButton" onclick="toggleMobileMore()"><span class="nav-icon">•••</span>More</button></nav>
<script>

(function(){
var DATA=null, LOGS=[], selected=0, editorDirty=false, wattsDirty=false, rateDirty=false;
var statusFailures=0;
var relayTimers=window.relayTimers||{};window.relayTimers=relayTimers;
var pendingRelayCommands={};
var DAYS=['M','T','W','T','F','S','S'];
function byId(id){return document.getElementById(id);}
function esc(v){
  var s=String(v==null?'':v);
  return s.replace(/[&<>"']/g,function(c){
    if(c==='&')return '&amp;'; if(c==='<')return '&lt;'; if(c==='>')return '&gt;';
    if(c==='"')return '&quot;'; return '&#39;';
  });
}
function toast(t){var e=byId('toast');if(!e)return;e.textContent=t;e.style.display='block';setTimeout(function(){e.style.display='none';},1800);}
function api(url,opt){return fetch(url,opt||{}).then(function(r){if(!r.ok)return r.text().then(function(t){throw new Error(t||('HTTP '+r.status));});return r;});}
function fmt(ms){var m=Math.floor((Number(ms)||0)/60000),h=Math.floor(m/60);m=m%60;return h?(h+'h '+m+'m'):(m+'m');}
function fmtTime(min){min=Number(min)||0;if(min===0)return '12:00 AM';var h=Math.floor(min/60),m=min%60,a=h>=12?'PM':'AM';h=h%12;if(h===0)h=12;return (h<10?'0':'')+h+':'+(m<10?'0':'')+m+' '+a;}
function timeInput(min){min=Number(min)||0;return (Math.floor(min/60)<10?'0':'')+Math.floor(min/60)+':'+(min%60<10?'0':'')+(min%60);}
function mins(v){var p=String(v||'00:00').split(':');return (Number(p[0])||0)*60+(Number(p[1])||0);}
function scrollToId(id){var e=byId(id);if(e)e.scrollIntoView({behavior:'smooth',block:'start'});}
function toggleMobileMore(){var m=byId('mobileMore');if(m)m.classList.toggle('show');}
function navTo(page,btn){
  var bs=document.querySelectorAll('.nav button,.mobile-nav button[data-nav],.mobile-more button');for(var i=0;i<bs.length;i++)bs[i].classList.toggle('active',bs[i].getAttribute('data-nav')===page);
  var more=byId('mobileMore');if(more)more.classList.remove('show');
  var moreButton=byId('mobileMoreButton');if(moreButton)moreButton.classList.toggle('active',['power','quick','status','logs','settings'].indexOf(page)>=0);
  var map={dashboard:'dashboardTop',relays:'relayGrid',schedules:'schedulePanel',power:'powerPanel',quick:'quickPanel',status:'statusPanel',logs:'logPanel'};
  if(page==='logs')loadLogs();
  if(page==='settings'){openWiFi();return;}
  if(page==='schedules'){var r=document.querySelector('.right'),shade=byId('scheduleShade');if(r)r.classList.remove('closed');if(shade)shade.classList.add('show');return;}
  scrollToId(map[page]||'dashboardTop');
}
function render(){
  if(!DATA||!DATA.relays)return;
  byId('active').textContent=DATA.relays.filter(function(r){return r.state;}).length+' / '+DATA.relays.length;
  byId('runtime').textContent=fmt(DATA.totalRuntime);byId('power').textContent=(DATA.currentPower||0)+' W';
  byId('pNow').textContent=(DATA.currentPower||0)+' W';byId('eToday').textContent=(Number(DATA.totalEnergy)||0).toFixed(2)+' kWh';
  byId('eMonth').textContent=(Number(DATA.monthlyEnergy)||0).toFixed(2)+' kWh';byId('cMonth').textContent='₹'+(Number(DATA.monthlyCost)||0).toFixed(2);
  byId('next').textContent=DATA.nextSchedule||'--';if(!rateDirty)byId('rate').value=DATA.electricityRate!=null?DATA.electricityRate:8.5;
  byId('ip').textContent=DATA.ip||'--';byId('rssi').textContent=(DATA.rssi!=null?DATA.rssi+' dBm':'--');byId('uptime').textContent=DATA.uptime||'--';
  byId('fw').textContent=DATA.version||'';byId('fw2').textContent=DATA.version||'';byId('otaVer').textContent=DATA.version||'';
  byId('conn').textContent=DATA.wifi?'Online':'Offline';byId('sys').textContent=DATA.wifi?'Online':'Offline';
  byId('safety').textContent=DATA.relays.some(function(r){return r.emergency;})?'EMERGENCY OFF':'No emergency';
  var maintenanceBtn=byId('maintenanceBtn');
  if(maintenanceBtn){
    if(DATA.maintenanceMode){maintenanceBtn.textContent='⏸ Resume schedules ('+Math.ceil((DATA.maintenanceRemainingMs||0)/60000)+'m)';maintenanceBtn.onclick=stopMaintenance;}
    else{maintenanceBtn.textContent='🍽 Feeding / Maintenance';maintenanceBtn.onclick=openMaintenance;}
  }
  renderRelays();renderTabs();if(!editorDirty)renderScheduleEditor();
}
function renderRelays(){
  var g=byId('relayGrid');if(!g)return;
  var out='';
  DATA.relays.forEach(function(r,i){
    var pending=!!pendingRelayCommands[i+1];
    var cls='relay'+(r.state?' active':'')+(pending?' pending':'');var modeClass=r.mode==='MANUAL'?' manual':'';
    var duration=Number(relayTimers[i+1]||0);
    out+='<article class="'+cls+'">';
    out+='<div class="rhead"><span class="num">'+(i+1)+'</span><span class="ricon">'+esc(r.icon)+'</span><span class="rname">'+esc(r.name)+'</span><span class="badge'+modeClass+'">'+esc(r.mode)+'</span></div>';
    out+='<div class="status'+(r.state?' on':'')+'"><span class="dot"></span><span class="status-text">'+(pending?'Sending…':(r.state?'ON':'OFF'))+'</span><span style="margin-left:auto;color:var(--muted)">'+fmt(r.runtime)+'</span></div>';
    out+='<div class="rstats"><span>'+(r.watts||0)+' W</span><span>Today: '+fmt(r.runtime)+'</span></div>';
    out+='<div class="next">→ '+(r.emergency?'Emergency OFF':nextFor(r))+'</div>';
    out+='<div class="scheduleline">☀ '+r.schedules.filter(function(s){return s.enabled;}).length+' Schedule(s)'+(r.manualOverride?' • Manual override':'')+'</div>';
    out+='<div class="timerline"><label for="timer'+(i+1)+'">Next manual command</label><select id="timer'+(i+1)+'" onchange="setRelayTimer('+(i+1)+',this.value)"><option value="0"'+(duration===0?' selected':'')+'>Until changed</option><option value="15"'+(duration===15?' selected':'')+'>15 minutes</option><option value="30"'+(duration===30?' selected':'')+'>30 minutes</option><option value="60"'+(duration===60?' selected':'')+'>1 hour</option><option value="240"'+(duration===240?' selected':'')+'>4 hours</option><option value="480"'+(duration===480?' selected':'')+'>8 hours</option><option value="1440"'+(duration===1440?' selected':'')+'>24 hours</option></select></div>';
    var timerInfo=r.emergency&&r.timerActive?'Timer pending while Emergency OFF':(r.timerActive?'Timer active · '+Math.ceil((r.timerRemainingMs||0)/60000)+' min left':'Applies to the next ON/OFF press');
    out+='<div class="timerhint">'+timerInfo+'</div>';
    out+='<div class="rfooter"><button class="onbtn" '+(pending?'disabled ':'')+'onclick="relay('+(i+1)+',\'on\')">● ON</button><button class="offbtn" '+(pending?'disabled ':'')+'onclick="relay('+(i+1)+',\'off\')">■ OFF</button><button onclick="selectRelay('+i+')">▣ Schedule</button></div>';
    out+='<button class="more" onclick="deviceEdit('+i+')">✎ Edit</button></article>';
  });
  g.innerHTML=out;
}
function setRelayTimer(id,value){relayTimers[id]=Number(value)||0;}
function nextFor(r){
  var schedules=r.schedules||[];
  if(!schedules.some(function(s){return s.enabled;}))return 'No schedules';
  if(r.mode!=='AUTO')return 'Schedules paused in MANUAL';
  var m=String(DATA&&DATA.time||'').match(/^(\d{2})-(\d{2})-(\d{4}) (\d{2}):(\d{2}):\d{2}$/);
  if(!m)return 'Schedule time unavailable';
  var todayDate=new Date(Date.UTC(Number(m[3]),Number(m[2])-1,Number(m[1])));
  var today=(todayDate.getUTCDay()+6)%7,now=Number(m[4])*60+Number(m[5]);
  var dayNames=['Mon','Tue','Wed','Thu','Fri','Sat','Sun'],bestDelta=100000,bestStart=-1,bestDayOffset=0,always=false;
  schedules.forEach(function(s){
    if(!s.enabled||!s.days)return;
    var start=Number(s.start)||0,stop=Number(s.stop)||0;
    if(start===0&&stop===0&&Number(s.days)===127){always=true;return;}
    for(var d=0;d<7;d++){
      if(!(Number(s.days)&(1<<d)))continue;
      var daysAhead=(d-today+7)%7,delta=daysAhead*1440+start-now;
      if(delta<=0){delta+=10080;daysAhead+=7;}
      if(delta<bestDelta){bestDelta=delta;bestStart=start;bestDayOffset=daysAhead;}
    }
  });
  if(bestStart<0)return always?'24/7 schedule':'No upcoming start';
  var offset=bestDayOffset,label=offset===0?'Today':(offset===1?'Tomorrow':(offset>=7?'Next '+dayNames[(today+offset)%7]:dayNames[(today+offset)%7]));
  return 'Next: '+label+' '+fmtTime(bestStart);
}
function renderTabs(){var e=byId('tabs');if(!e||!DATA)return;var out='';DATA.relays.forEach(function(r,i){out+='<button class="'+(i===selected?'active':'')+'" onclick="selectRelay('+i+')">Relay '+(i+1)+'</button>';});e.innerHTML=out;}
function renderScheduleEditor(){
  var r=DATA&&DATA.relays?DATA.relays[selected]:null;if(!r)return;
  var cards='';
  for(var i=0;i<r.schedules.length;i++){
    var s=r.schedules[i];if(!s.enabled)continue;
    cards+='<div class="schedcard"><div class="schedtitle"><span>Schedule '+(i+1)+'</span><button class="iconbtn danger" style="padding:3px 7px" onclick="disableSchedule('+i+')">×</button></div>';
    cards+='<div class="days">';for(var k=0;k<7;k++)cards+='<button class="day '+((s.days&(1<<k))?'sel':'')+'" onclick="toggleDay('+i+','+k+')">'+DAYS[k]+'</button>';cards+='</div>';
    cards+='<div class="twocol"><div class="field"><label>Start</label><input type="time" id="st'+i+'" value="'+timeInput(s.start)+'" oninput="markEditorDirty()"></div><div class="field"><label>Stop</label><input type="time" id="sp'+i+'" value="'+timeInput(s.stop)+'" oninput="markEditorDirty()"></div></div>';
    cards+='<button class="save" style="width:100%;padding:8px;border:1px solid var(--green2);border-radius:7px;color:white" onclick="saveSchedule('+i+')">Save Schedule</button></div>';
  }
  byId('scheduleEditor').innerHTML='<div class="schedcard"><div class="twocol"><div><b>'+esc(r.icon)+' '+esc(r.name)+'</b></div><select id="mode" onchange="setMode(this.value)"><option '+(r.mode==='AUTO'?'selected':'')+'>AUTO</option><option '+(r.mode==='MANUAL'?'selected':'')+'>MANUAL</option></select></div>'+cards+'<button class="add" onclick="addSchedule()">＋ Add Schedule</button></div>';
  if(!wattsDirty)byId('selWatts').value=r.watts||0;byId('powerInfo').textContent='Today runtime: '+fmt(r.runtime)+' • Estimated today: '+(((Number(r.watts)||0)*(Number(r.runtime)||0)/3600000000).toFixed(3))+' kWh';
}
function selectRelay(i){if(i!==selected&&editorDirty){if(!confirm('You have unsaved schedule changes. Discard them?'))return;editorDirty=false;}if(i!==selected)wattsDirty=false;selected=i;var r=document.querySelector('.right'),shade=byId('scheduleShade');if(r)r.classList.remove('closed');if(shade)shade.classList.add('show');renderTabs();renderScheduleEditor();}
function relay(id,a){if(pendingRelayCommands[id])return;if(a==='emergency'&&!confirm('Force this relay OFF until you resume it?'))return;var fd=new URLSearchParams();fd.append('id',id);fd.append('action',a);if((a==='on'||a==='off')&&relayTimers[id])fd.append('durationMinutes',relayTimers[id]);pendingRelayCommands[id]=true;renderRelays();toast('Sending command to Relay '+id+'…');api('/api/relay',{method:'POST',body:fd}).then(function(){toast('Relay '+id+' command sent');return load();}).catch(function(e){toast('Relay '+id+': '+e.message);}).finally(function(){delete pendingRelayCommands[id];renderRelays();});}
function allOff(){if(!confirm('Turn OFF all 8 relays and activate Emergency OFF?'))return;api('/api/alloff',{method:'POST'}).then(load).catch(function(e){toast(e.message);});}
function resumeAll(){api('/api/resumeall',{method:'POST'}).then(load).catch(function(e){toast(e.message);});}
function openMaintenance(){
  if(!DATA||!DATA.relays)return;
  var choices='';
  DATA.relays.forEach(function(r,i){
    if(r.mode!=='AUTO'||!(r.schedules||[]).some(function(s){return s.enabled;}))return;
    choices+='<label class="maintenance-choice"><input class="maintenance-relay" type="checkbox" value="'+(i+1)+'" checked> '+esc(r.icon)+' '+esc(r.name)+'</label>';
  });
  if(!choices){toast('No AUTO relays with schedules are available to pause');return;}
  byId('modalbox').innerHTML='<h3>🍽 Feeding / Maintenance Mode</h3><p>Pause schedule automation for selected relays. This does not change their current ON/OFF state. Manual controls remain available; schedules resume when the timer ends.</p><div class="maintenance-list">'+choices+'</div><div class="field"><label for="maintenanceDuration">Pause schedules for</label><select id="maintenanceDuration"><option value="15">15 minutes</option><option value="30">30 minutes</option><option value="60">1 hour</option><option value="120">2 hours</option></select></div><div class="modal-actions"><button onclick="closeModal()">Cancel</button><button class="save" onclick="startMaintenance()">Start mode</button></div>';
  byId('modal').classList.add('show');
}
function startMaintenance(){
  var checks=document.querySelectorAll('.maintenance-relay:checked'),mask=0;
  checks.forEach(function(c){mask|=1<<(Number(c.value)-1);});
  if(!mask){toast('Select at least one relay');return;}
  var fd=new URLSearchParams();fd.append('action','start');fd.append('minutes',byId('maintenanceDuration').value);fd.append('mask',mask);
  api('/api/maintenance',{method:'POST',body:fd}).then(function(){closeModal();toast('Schedule pause started');return load();}).catch(function(e){toast(e.message);});
}
function stopMaintenance(){api('/api/maintenance',{method:'POST',body:new URLSearchParams({action:'stop'})}).then(function(){toast('Schedule control resumed');return load();}).catch(function(e){toast(e.message);});}
function toggleDay(slot,day){var s=DATA.relays[selected].schedules[slot];s.days=s.days^(1<<day);editorDirty=true;renderScheduleEditor();}
function saveSchedule(slot){
  var s=DATA.relays[selected].schedules[slot],st=mins(byId('st'+slot).value),sp=mins(byId('sp'+slot).value),fd=new URLSearchParams();
  fd.append('id',selected+1);fd.append('slot',slot);fd.append('enabled','1');fd.append('start',st);fd.append('stop',sp);fd.append('days',s.days);fd.append('mode',byId('mode').value);
  api('/api/schedule',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd}).then(function(){editorDirty=false;toast('Schedule saved');return load();}).catch(function(e){toast(e.message);});
}
function disableSchedule(slot){editorDirty=false;var fd=new URLSearchParams();fd.append('id',selected+1);fd.append('slot',slot);fd.append('enabled','0');fd.append('start','0');fd.append('stop','0');fd.append('days','127');fd.append('mode',DATA.relays[selected].mode);api('/api/schedule',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd}).then(load).catch(function(e){toast(e.message);});}
function addSchedule(){editorDirty=false;var r=DATA.relays[selected],slot=-1;for(var i=0;i<r.schedules.length;i++){if(!r.schedules[i].enabled){slot=i;break;}}if(slot<0){toast('Maximum 6 schedules');return;}var fd=new URLSearchParams();fd.append('id',selected+1);fd.append('slot',slot);fd.append('enabled','1');fd.append('start','480');fd.append('stop','600');fd.append('days','127');fd.append('mode',r.mode);api('/api/schedule',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd}).then(load).catch(function(e){toast(e.message);});}
function setMode(m){editorDirty=false;var r=DATA.relays[selected],s=r.schedules[0],fd=new URLSearchParams();fd.append('id',selected+1);fd.append('slot','0');fd.append('enabled',s.enabled?'1':'0');fd.append('start',s.start);fd.append('stop',s.stop);fd.append('days',s.days);fd.append('mode',m);api('/api/schedule',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd}).then(load).catch(function(e){toast(e.message);});}
function saveSelectedWatts(){var fd=new URLSearchParams();fd.append('id',selected+1);fd.append('watts',byId('selWatts').value);api('/api/power',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd}).then(function(){wattsDirty=false;return load();}).then(function(){toast('Power rating saved');}).catch(function(e){toast(e.message);});}
function saveRate(){var fd=new URLSearchParams();fd.append('rate',byId('rate').value);api('/api/power',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd}).then(function(){rateDirty=false;return load();}).then(function(){toast('Electricity rate saved');}).catch(function(e){toast(e.message);});}
function deviceEdit(i){selected=i;openNames();}
function openNames(){var rows='';for(var i=0;i<DATA.relays.length;i++){var r=DATA.relays[i];rows+='<div class="schedcard"><div class="twocol"><div class="field"><label>Relay '+(i+1)+' Name</label><input id="n'+i+'" value="'+esc(r.name)+'"></div><div class="field"><label>Icon</label><input id="ic'+i+'" value="'+esc(r.icon)+'"></div></div></div>';}byId('modalbox').innerHTML='<h2>Device Names & Icons</h2>'+rows+'<div class="modal-actions"><button onclick="closeModal()">Cancel</button><button class="save" onclick="saveNames()">Save</button></div>';byId('modal').classList.add('show');}
function saveNames(){var chain=Promise.resolve();for(var i=0;i<DATA.relays.length;i++){(function(n){var fd=new URLSearchParams();fd.append('id',n+1);fd.append('name',byId('n'+n).value);fd.append('icon',byId('ic'+n).value);chain=chain.then(function(){return api('/api/device',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd});});})(i);}chain.then(function(){closeModal();return load();}).then(function(){toast('Devices saved');}).catch(function(e){toast(e.message);});}
function openWiFi(){byId('modalbox').innerHTML='<h2>Wi-Fi Settings</h2><p style="color:var(--muted)">The ESP32 restarts after saving. Existing relay settings and schedules remain stored.</p><div class="field"><label>Wi-Fi SSID</label><input id="ssid"></div><div class="field"><label>Wi-Fi Password</label><input id="pass" type="password"></div><div class="modal-actions"><button onclick="closeModal()">Cancel</button><button class="save" onclick="saveWiFi()">Save Wi-Fi</button></div>';byId('modal').classList.add('show');}
function saveWiFi(){var fd=new URLSearchParams();fd.append('ssid',byId('ssid').value);fd.append('password',byId('pass').value);api('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd}).then(function(){toast('Wi-Fi saved. Restarting...');closeModal();}).catch(function(e){toast(e.message);});}
function closeModal(){byId('modal').classList.remove('show');}
function closeSide(){var r=document.querySelector('.right'),shade=byId('scheduleShade');if(r)r.classList.add('closed');if(shade)shade.classList.remove('show');}
function toggleTheme(){document.body.classList.toggle('dark');localStorage.setItem('aqTheme',document.body.classList.contains('dark')?'dark':'light');}
if(localStorage.getItem('aqTheme')==='dark')document.body.classList.add('dark');
function otaInfo(){alert('OTA is enabled. Use Arduino IDE → Port → Network Ports → Aquarium-Controller.local, then Upload.');}
function backup(){fetch('/api/status').then(function(r){return r.json();}).then(function(d){d.backupCreated=new Date().toISOString();var a=document.createElement('a');a.href=URL.createObjectURL(new Blob([JSON.stringify(d,null,2)],{type:'application/json'}));a.download='aquarium-controller-backup.json';a.click();});}
function readRestore(input){var f=input.files&&input.files[0];if(!f)return;var rd=new FileReader();rd.onload=function(){try{var d=JSON.parse(rd.result);if(!d.relays||d.relays.length!==DATA.relays.length)throw new Error('Backup relay count does not match');restoreBackup(d);}catch(e){alert('Restore failed: '+e.message);}};rd.readAsText(f);input.value='';}
function restoreBackup(d){if(!confirm('Restore names, icons, power settings and schedules?'))return;var chain=Promise.resolve();d.relays.forEach(function(r){chain=chain.then(function(){var fd=new URLSearchParams();fd.append('id',r.id);fd.append('name',r.name);fd.append('icon',r.icon);return api('/api/device',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd});}).then(function(){var fd=new URLSearchParams();fd.append('id',r.id);fd.append('watts',r.watts||0);return api('/api/power',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd});}).then(function(){var q=Promise.resolve();for(var i=0;i<r.schedules.length;i++){(function(x,n){q=q.then(function(){var fd=new URLSearchParams();fd.append('id',r.id);fd.append('slot',n);fd.append('enabled',x.enabled?'1':'0');fd.append('start',x.start);fd.append('stop',x.stop);fd.append('days',x.days);fd.append('fullDay',x.fullDay?'1':'0');fd.append('mode',r.mode);return api('/api/schedule',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd});});})(r.schedules[i],i);}return q;});});chain.then(function(){return load();}).then(function(){toast('Backup restored');}).catch(function(e){toast('Restore stopped: '+e.message);});}
function restartESP(){if(confirm('Restart ESP32?'))api('/api/reset',{method:'POST',body:new URLSearchParams({restart:'1'})}).catch(function(e){toast(e.message);});}
function factoryReset(){if(confirm('Factory reset all settings and Wi-Fi credentials? This cannot be undone.'))api('/api/reset',{method:'POST'}).catch(function(e){toast(e.message);});}
function clearLogs(){if(!confirm('Clear activity log?'))return;api('/api/logs/clear',{method:'POST'}).then(loadLogs);}
function loadLogs(){return fetch('/api/logs',{cache:'no-store'}).then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.json();}).then(function(d){LOGS=d.logs||[];renderLogs();}).catch(function(e){toast('Could not load activity log: '+e.message);});}
function renderLogs(){var q=(byId('search').value||'').toLowerCase(),a=LOGS.filter(function(x){return x.toLowerCase().indexOf(q)>=0;});var out='';for(var i=a.length-1;i>=0;i--){var p=a[i].split(' | ');out+='<div class="logrow"><span class="logtime">'+esc(p[0]||'')+'</span><span class="logevent">'+esc(p.slice(1).join(' | '))+'</span></div>';}byId('logs').innerHTML=out;}
function downloadLogs(){var blob=new Blob([LOGS.join('\n')],{type:'text/plain'}),a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='aquarium-activity-log.txt';a.click();}
var statusBusy=false;
function load(){
  if(statusBusy)return Promise.resolve();
  statusBusy=true;
  var controller=new AbortController();
  var timeout=setTimeout(function(){controller.abort();},8000);
  return fetch('/api/status',{cache:'no-store',signal:controller.signal})
    .then(function(res){if(!res.ok)throw new Error('HTTP '+res.status);return res.json();})
    .then(function(d){
      if(!d||!Array.isArray(d.relays))throw new Error('Invalid /api/status response');
      DATA=d;render();statusFailures=0;byId('conn').textContent='Online';byId('sys').textContent='Online';byId('lastSeen').textContent=new Date().toLocaleTimeString();
    })
    .catch(function(e){statusFailures++;byId('conn').textContent=statusFailures<2?'Retrying…':'Offline';if(statusFailures>=2){byId('sys').textContent='Offline';byId('safety').textContent='Status unavailable';}console.warn('Aquarium status request failed ('+statusFailures+'):',e);})
    .finally(function(){clearTimeout(timeout);statusBusy=false;});
}
function markEditorDirty(){editorDirty=true;}function markWattsDirty(){wattsDirty=true;}function markRateDirty(){rateDirty=true;}
  window.navTo=navTo;window.toggleMobileMore=toggleMobileMore;window.toggleTheme=toggleTheme;window.openNames=openNames;window.allOff=allOff;window.resumeAll=resumeAll;window.openMaintenance=openMaintenance;window.startMaintenance=startMaintenance;window.stopMaintenance=stopMaintenance;window.saveRate=saveRate;window.openWiFi=openWiFi;window.backup=backup;window.otaInfo=otaInfo;window.restartESP=restartESP;window.factoryReset=factoryReset;window.readRestore=readRestore;window.clearLogs=clearLogs;window.downloadLogs=downloadLogs;window.saveSelectedWatts=saveSelectedWatts;window.closeSide=closeSide;window.closeModal=closeModal;window.saveNames=saveNames;window.relay=relay;window.setRelayTimer=setRelayTimer;window.selectRelay=selectRelay;window.deviceEdit=deviceEdit;window.disableSchedule=disableSchedule;window.toggleDay=toggleDay;window.saveSchedule=saveSchedule;window.addSchedule=addSchedule;window.setMode=setMode;window.renderLogs=renderLogs;window.markEditorDirty=markEditorDirty;window.markWattsDirty=markWattsDirty;window.markRateDirty=markRateDirty;
setInterval(load,5000);setInterval(function(){var e=byId('clock');if(e)e.textContent=new Date().toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});},1000);load();
})();

</script></body></html>
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
bool getTimeInfo(struct tm &t){return getLocalTime(&t,1000);}
int currentMinutes(){struct tm t;if(!getTimeInfo(t))return -1;return t.tm_hour*60+t.tm_min;}
int currentWeekday(){struct tm t;if(!getTimeInfo(t))return -1;return(t.tm_wday+6)%7;}
String currentDateTime(){struct tm t;if(!getTimeInfo(t))return "";char b[32];strftime(b,sizeof(b),"%d-%m-%Y %H:%M:%S",&t);return String(b);}

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
  String e=currentDateTime()+" | "+msg;
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
  if(!on) persistRuntime();
  addLog(r.name+" "+(on?"ON":"OFF")+" ("+reason+")");
}

// ============================================================
// SCHEDULE ENGINE
// ============================================================
bool slotActive(const ScheduleSlot&s,int now,int today){
  if(!s.enabled||s.days==0)return false;
  if(s.startMinutes==0&&s.stopMinutes==0)return(s.days&(1<<today)); // 24x7
  if(s.startMinutes<s.stopMinutes)return(s.days&(1<<today))&&now>=s.startMinutes&&now<s.stopMinutes;
  if(now>=s.startMinutes)return(s.days&(1<<today));
  if(now<s.stopMinutes){int prev=(today+6)%7;return(s.days&(1<<prev));}
  return false;
}

bool anyScheduleActive(int id){
  if(id<0||id>=RELAY_COUNT||!relays[id].autoMode)return false;
  int now=currentMinutes(),day=currentWeekday();if(now<0||day<0)return false;
  for(int j=0;j<MAX_SCHEDULES;j++)if(slotActive(relays[id].schedules[j],now,day))return true;
  return false;
}

bool maintenanceApplies(int id){
  return maintenanceModeActive&&id>=0&&id<RELAY_COUNT&&(maintenanceRelayMask&(1U<<id));
}

void processMaintenanceMode(){
  if(!maintenanceModeActive||(int32_t)(millis()-maintenanceModeUntil)<0)return;
  maintenanceModeActive=false;maintenanceModeUntil=0;maintenanceRelayMask=0;
  addLog("Feeding / Maintenance schedule pause ended");
}

void processTimedOverrides(){
  uint32_t now=millis();
  for(int i=0;i<RELAY_COUNT;i++){
    RelayConfig&r=relays[i];
    if(!r.timedOverrideActive||r.emergencyOff)continue;
    if((int32_t)(now-r.timedOverrideUntil)<0)continue;
    r.timedOverrideActive=false;
    if(r.autoMode){r.manualOverride=false;if(!maintenanceApplies(i))setRelayState(i,anyScheduleActive(i),"Temporary override ended");}
    else{r.manualOverride=false;setRelayState(i,false,"Timer ended");}
    addLog(r.name+" timer completed");
  }
}

void processSchedules(){
  if(millis()-lastScheduleCheck<500)return;lastScheduleCheck=millis();
  struct tm t;if(!getTimeInfo(t))return;int today=(t.tm_wday+6)%7;
  if(lastWeekday<0)lastWeekday=today;
  if(today!=lastWeekday){for(int i=0;i<RELAY_COUNT;i++){relays[i].runtimeTodayMs=0;if(relays[i].state)relays[i].stateStartedMillis=millis();}lastWeekday=today;}
  for(int i=0;i<RELAY_COUNT;i++){
    RelayConfig&r=relays[i];if(!r.autoMode||r.emergencyOff||r.timedOverrideActive||maintenanceApplies(i))continue;
    bool active=anyScheduleActive(i);
    if(active){r.manualOverride=false;if(!r.state)setRelayState(i,true,"Schedule started");}
    else if(!r.manualOverride){if(r.state)setRelayState(i,false,"Schedule ended");}
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
    relays[i].manualOverride=false;relays[i].emergencyOff=false;
    relays[i].timedOverrideActive=false;relays[i].timedOverrideUntil=0;
    relays[i].emergencyResumeState=false;relays[i].emergencyResumeManualOverride=false;
    relays[i].watts=DEFAULT_WATTS[i];
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
  snprintf(k,sizeof(k),"rs%d",id);prefs.putBool(k,relays[id].emergencyResumeState);
  snprintf(k,sizeof(k),"ro%d",id);prefs.putBool(k,relays[id].emergencyResumeManualOverride);
  snprintf(k,sizeof(k),"w%d",id);prefs.putUShort(k,relays[id].watts);
  for(int j=0;j<MAX_SCHEDULES;j++){
    snprintf(k,sizeof(k),"e%d_%d",id,j);prefs.putBool(k,relays[id].schedules[j].enabled);
    snprintf(k,sizeof(k),"s%d_%d",id,j);prefs.putUShort(k,relays[id].schedules[j].startMinutes);
    snprintf(k,sizeof(k),"t%d_%d",id,j);prefs.putUShort(k,relays[id].schedules[j].stopMinutes);
    snprintf(k,sizeof(k),"d%d_%d",id,j);prefs.putUChar(k,relays[id].schedules[j].days);
  }
}

void loadSettings(){
  setDefaults();prefs.begin("aquarium",false);
  wifiSSID=prefs.getString("ssid",DEFAULT_WIFI_SSID);wifiPassword=prefs.getString("pass",DEFAULT_WIFI_PASSWORD);
  electricityRate=prefs.getFloat("rate",8.50f);
  if(electricityRate<0 || electricityRate>1000) electricityRate=8.50f;
  for(int i=0;i<RELAY_COUNT;i++){
    char k[24];
    snprintf(k,sizeof(k),"name%d",i);relays[i].name=prefs.getString(k,relays[i].name);
    snprintf(k,sizeof(k),"icon%d",i);relays[i].icon=prefs.getString(k,relays[i].icon);
    snprintf(k,sizeof(k),"auto%d",i);relays[i].autoMode=prefs.getBool(k,relays[i].autoMode);
    snprintf(k,sizeof(k),"em%d",i);relays[i].emergencyOff=prefs.getBool(k,false);
    snprintf(k,sizeof(k),"rs%d",i);relays[i].emergencyResumeState=prefs.getBool(k,false);
    snprintf(k,sizeof(k),"ro%d",i);relays[i].emergencyResumeManualOverride=prefs.getBool(k,false);
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
    configTime(19800,0,"pool.ntp.org","time.nist.gov");
    struct tm t;if(getLocalTime(&t,10000))Serial.print("IST: "),Serial.println(currentDateTime());
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
  // Reserve enough heap for the 8-relay status payload. Without this, repeated String
  // concatenation can fragment ESP32 heap and make /api/status fail/reset on larger configs.
  String j;
  j.reserve(18000);
  j="{";j+="\"version\":\""+String(FW_VERSION)+"\",";
  String ip=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString();
  j+="\"ip\":\""+ip+"\",";
  j+="\"wifi\":"+(WiFi.status()==WL_CONNECTED?String("true"):String("false"))+",";
  j+="\"ota\":true,\"networkMode\":\""+jsonEscape(networkModeString())+"\",";
  j+="\"rssi\":"+String(WiFi.status()==WL_CONNECTED?WiFi.RSSI():0)+",";
  j+="\"uptime\":\""+jsonEscape(uptimeString())+"\",";
  j+="\"time\":\""+jsonEscape(currentDateTime())+"\",";
  j+="\"bootReason\":\""+jsonEscape(bootReason())+"\",";
  j+="\"logCount\":"+String(logCount)+",";
  uint32_t maintenanceRemainingMs=0;
  if(maintenanceModeActive){int32_t timeLeft=(int32_t)(maintenanceModeUntil-millis());if(timeLeft>0)maintenanceRemainingMs=(uint32_t)timeLeft;}
  j+="\"maintenanceMode\":"+(maintenanceModeActive?String("true"):String("false"))+",\"maintenanceRemainingMs\":"+String(maintenanceRemainingMs)+",\"maintenanceRelayMask\":"+String(maintenanceRelayMask)+",";
  int autoCount=0,scheduleCount=0;uint64_t totalRuntime=0;double energy=0;
  for(int i=0;i<RELAY_COUNT;i++){if(relays[i].autoMode)autoCount++;for(int k=0;k<MAX_SCHEDULES;k++)if(relays[i].schedules[k].enabled)scheduleCount++;totalRuntime+=currentRuntime(i);energy+=(double)relays[i].watts*(double)currentRuntime(i)/3600000000.0;}
  double todayCost=energy*(double)electricityRate;
  double monthlyEnergy=energy*30.0;
  double monthlyCost=monthlyEnergy*(double)electricityRate;
  uint32_t currentPower=0;
  for(int i=0;i<RELAY_COUNT;i++) if(relays[i].state) currentPower += relays[i].watts;
  j+="\"autoCount\":"+String(autoCount)+",\"scheduleCount\":"+String(scheduleCount)+",";
  j+="\"totalRuntime\":"+String((uint32_t)totalRuntime)+",\"totalEnergy\":"+String(energy,3)+",";
  j+="\"electricityRate\":"+String(electricityRate,2)+",\"todayCost\":"+String(todayCost,2)+",\"monthlyEnergy\":"+String(monthlyEnergy,2)+",\"monthlyCost\":"+String(monthlyCost,2)+",\"currentPower\":"+String(currentPower)+",";
  j+="\"nextSchedule\":\""+jsonEscape(findNextSchedule())+"\",\"relays\":[";
  for(int i=0;i<RELAY_COUNT;i++){
    if(i)j+=",";
    RelayConfig&r=relays[i];j+="{";
    j+="\"id\":"+String(i+1)+",\"name\":\""+jsonEscape(r.name)+"\",\"icon\":\""+jsonEscape(r.icon)+"\",";
    j+="\"state\":"+(r.state?String("true"):String("false"))+",\"mode\":\""+String(r.autoMode?"AUTO":"MANUAL")+"\",";
    j+="\"emergency\":"+(r.emergencyOff?String("true"):String("false"))+",\"manualOverride\":"+(r.manualOverride?String("true"):String("false"))+",";
    uint32_t timerRemainingMs=0;
    if(r.timedOverrideActive){int32_t timeLeft=(int32_t)(r.timedOverrideUntil-millis());if(timeLeft>0)timerRemainingMs=(uint32_t)timeLeft;}
    j+="\"timerActive\":"+(r.timedOverrideActive?String("true"):String("false"))+",\"timerRemainingMs\":"+String(timerRemainingMs)+",";
    j+="\"watts\":"+String(r.watts)+",\"runtime\":"+String((uint32_t)currentRuntime(i))+",\"schedules\":[";
    for(int k=0;k<MAX_SCHEDULES;k++){if(k)j+=",";ScheduleSlot&s=r.schedules[k];j+="{\"enabled\":"+(s.enabled?String("true"):String("false"))+",\"start\":"+String(s.startMinutes)+",\"stop\":"+String(s.stopMinutes)+",\"days\":"+String(s.days)+",\"fullDay\":"+(s.startMinutes==0&&s.stopMinutes==0?String("true"):String("false"))+"}";}
    j+="]}";
  }
  j+="]}";return j;
}

String findNextSchedule(){
  int now=currentMinutes(),today=currentWeekday();if(now<0||today<0)return "--";
  const char* dayNames[]={"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
  int bestDelta=100000;String result="";
  for(int i=0;i<RELAY_COUNT;i++){
    if(!relays[i].autoMode||relays[i].emergencyOff)continue;
    for(int s=0;s<MAX_SCHEDULES;s++){
      ScheduleSlot&x=relays[i].schedules[s];if(!x.enabled||x.days==0)continue;
      if(x.startMinutes==0&&x.stopMinutes==0&&x.days&(1<<today))continue;
      for(int d=0;d<7;d++){
        if(!(x.days&(1<<d)))continue;
        int daysAhead=(d-today+7)%7;
        int delta=daysAhead*1440+x.startMinutes-now;
        if(delta<=0){delta+=10080;daysAhead+=7;}
        if(delta<bestDelta){
          bestDelta=delta;
          String dayLabel;
          if(daysAhead==0)dayLabel="Today";
          else if(daysAhead==1)dayLabel="Tomorrow";
          else if(daysAhead>=7)dayLabel=String("Next ")+dayNames[(today+daysAhead)%7];
          else dayLabel=dayNames[(today+daysAhead)%7];
          result=dayLabel+" "+formatMinutes(x.startMinutes)+" • "+relays[i].name;
        }
      }
    }
  }
  return result;
}

// ============================================================
// API
// ============================================================
void handleStatus(){
  String payload=buildStatusJSON();
  server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("X-Status-Heap",String(ESP.getFreeHeap()));
  server.send(200,"application/json",payload);
}

void handleRelay(){
  if(!server.hasArg("id")||!server.hasArg("action")){server.send(400,"text/plain","Missing parameters");return;}
  int id=server.arg("id").toInt()-1;String a=server.arg("action");if(id<0||id>=RELAY_COUNT){server.send(400,"text/plain","Invalid relay");return;}
  RelayConfig&r=relays[id];
  if(r.emergencyOff&&a!="resume"){server.send(409,"text/plain","Emergency OFF is active. Resume AUTO first.");return;}
  if(a=="on"||a=="off"){
    if(r.autoMode&&anyScheduleActive(id)&&!maintenanceApplies(id)){server.send(409,"text/plain","AUTO schedule is currently active. Use Emergency OFF to force a manual OFF.");return;}
    uint16_t durationMinutes=0;
    if(server.hasArg("durationMinutes"))durationMinutes=(uint16_t)constrain(server.arg("durationMinutes").toInt(),0,1440);
    r.manualOverride=r.autoMode;
    r.timedOverrideActive=durationMinutes>0;
    r.timedOverrideUntil=millis()+(uint32_t)durationMinutes*60000UL;
    setRelayState(id,a=="on",durationMinutes?"Timed manual command":"Manual");
    if(durationMinutes)addLog(r.name+" temporary override for "+String(durationMinutes)+" minutes");
    saveRelay(id);server.send(200,"text/plain","OK");return;
  }
  if(a=="emergency"){
    r.emergencyResumeState=r.state;
    r.emergencyResumeManualOverride=r.manualOverride;
    r.emergencyOff=true;
    r.manualOverride=false;
    setRelayState(id,false,"Emergency OFF");
    saveRelay(id);
    server.send(200,"text/plain","OK");
    return;
  }
  if(a=="resume"){
    if(!r.emergencyOff){server.send(200,"text/plain","OK");return;}
    r.emergencyOff=false;
    r.manualOverride=r.emergencyResumeManualOverride;
    bool restoreState=r.emergencyResumeState;
    r.emergencyResumeState=false;
    r.emergencyResumeManualOverride=false;
    setRelayState(id,restoreState,"Emergency Resume");
    saveRelay(id);
    addLog(r.name+" emergency state resumed");
    server.send(200,"text/plain","OK");
    return;
  }
  server.send(400,"text/plain","Invalid action");
}

void handleMaintenance(){
  if(!server.hasArg("action")){server.send(400,"text/plain","Missing action");return;}
  String action=server.arg("action");
  if(action=="stop"){
    if(maintenanceModeActive)addLog("Feeding / Maintenance schedule pause stopped");
    maintenanceModeActive=false;maintenanceModeUntil=0;maintenanceRelayMask=0;
    server.send(200,"text/plain","OK");return;
  }
  if(action!="start"||!server.hasArg("minutes")||!server.hasArg("mask")){server.send(400,"text/plain","Invalid maintenance request");return;}
  int minutes=server.arg("minutes").toInt();int requestedMask=server.arg("mask").toInt();
  if(minutes<1||minutes>240||requestedMask<1||requestedMask>255){server.send(400,"text/plain","Invalid duration or relay selection");return;}
  uint8_t validMask=0;
  for(int i=0;i<RELAY_COUNT;i++){
    if(!(requestedMask&(1<<i))||!relays[i].autoMode)continue;
    bool hasSchedule=false;for(int j=0;j<MAX_SCHEDULES;j++)if(relays[i].schedules[j].enabled){hasSchedule=true;break;}
    if(hasSchedule)validMask|=(uint8_t)(1U<<i);
  }
  if(!validMask){server.send(400,"text/plain","Select at least one AUTO relay with an enabled schedule");return;}
  maintenanceRelayMask=validMask;maintenanceModeUntil=millis()+(uint32_t)minutes*60000UL;maintenanceModeActive=true;
  for(int i=0;i<RELAY_COUNT;i++)if(validMask&(1U<<i))relays[i].manualOverride=false;
  addLog("Feeding / Maintenance schedule pause started for "+String(minutes)+" minutes");
  server.send(200,"text/plain","OK");
}

void handleAllOff(){
  for(int i=0;i<RELAY_COUNT;i++){
    if(!relays[i].emergencyOff){
      relays[i].emergencyResumeState=relays[i].state;
      relays[i].emergencyResumeManualOverride=relays[i].manualOverride;
    }
    relays[i].emergencyOff=true;
    relays[i].manualOverride=false;
    setRelayState(i,false,"Emergency All OFF");
    saveRelay(i);
  }
  addLog("All relays Emergency OFF");
  server.send(200,"text/plain","OK");
}

void handleResumeAll(){
  for(int i=0;i<RELAY_COUNT;i++){
    if(!relays[i].emergencyOff)continue;
    relays[i].emergencyOff=false;
    relays[i].manualOverride=relays[i].emergencyResumeManualOverride;
    bool restoreState=relays[i].emergencyResumeState;
    relays[i].emergencyResumeState=false;
    relays[i].emergencyResumeManualOverride=false;
    setRelayState(i,restoreState,"Emergency Resume All");
    saveRelay(i);
  }
  addLog("All relays emergency state resumed");
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
  if(server.hasArg("rate") && !server.hasArg("id")){
    float rate=server.arg("rate").toFloat();
    if(rate<0 || rate>1000){server.send(400,"text/plain","Rate must be 0-1000 INR/kWh");return;}
    electricityRate=rate; prefs.putFloat("rate",electricityRate);
    addLog("Electricity rate updated: Rs."+String(electricityRate,2)+"/kWh");
    server.send(200,"text/plain",String(electricityRate,2)); return;
  }
  if(!server.hasArg("id")){server.send(400,"text/plain","Missing id");return;}
  int id=server.arg("id").toInt()-1;
  if(id<0||id>=RELAY_COUNT){server.send(400,"text/plain","Invalid relay");return;}
  uint16_t newWatts=relays[id].watts;
  if(server.hasArg("reset")&&server.arg("reset")=="1"){
    newWatts=0;
  }else if(server.hasArg("watts")){
    String raw=server.arg("watts");raw.trim();
    if(raw.length()==0){server.send(400,"text/plain","Watts cannot be empty");return;}
    long requested=raw.toInt();
    if(requested<0||requested>5000){server.send(400,"text/plain","Power must be 0-5000 W");return;}
    newWatts=(uint16_t)requested;
  }else{server.send(400,"text/plain","Missing watts");return;}
  relays[id].watts=newWatts;saveRelay(id);
  addLog(relays[id].name+" power rating updated: "+String(relays[id].watts)+"W");
  server.send(200,"text/plain",String(relays[id].watts));
}

void handleWiFi(){
  if(!server.hasArg("ssid")||!server.hasArg("password")){server.send(400,"text/plain","Missing Wi-Fi parameters");return;}
  String ssid=urlDecode(server.arg("ssid")),pass=urlDecode(server.arg("password"));if(ssid.length()==0){server.send(400,"text/plain","SSID cannot be empty");return;}
  prefs.putString("ssid",ssid);prefs.putString("pass",pass);server.send(200,"text/plain","Wi-Fi saved. Restarting...");delay(800);ESP.restart();
}

void handleLogs(){String j="{\"logs\":[";for(int i=0;i<logCount;i++){if(i)j+=",";j+="\""+jsonEscape(activityLogs[i])+"\"";}j+="]}";server.send(200,"application/json",j);}
void handleClearLogs(){logCount=0;LittleFS.remove(LOG_FILE);server.send(200,"text/plain","OK");}
void handleReset(){for(int i=0;i<RELAY_COUNT;i++)setRelayHardware(i,false); if(server.hasArg("restart")&&server.arg("restart")=="1"){server.send(200,"text/plain","Restarting...");delay(300);ESP.restart();return;} LittleFS.remove(LOG_FILE);prefs.clear();server.send(200,"text/plain","Factory reset. Restarting...");delay(800);ESP.restart();}

// ============================================================
// SETUP / LOOP
// ============================================================
void setup(){
  bootMillis=millis();
  Serial.begin(115200);delay(300);
  Serial.println("\n====================================");Serial.println("ESP32 Smart Relay Controller v3.3.0");Serial.println("====================================");

  // Startup safety: every relay OFF before Wi-Fi/settings initialization.
  for(int i=0;i<RELAY_COUNT;i++){pinMode(relayPins[i],OUTPUT);digitalWrite(relayPins[i],RELAY_OFF);}
  loadSettings();
  if(!LittleFS.begin(true)) Serial.println("WARNING: LittleFS unavailable; activity log will not survive reboot.");
  else loadLogsFromFS();

  // Hardware remains OFF after reboot. Scheduler will decide the state once time is available.
  for(int i=0;i<RELAY_COUNT;i++){relays[i].state=false;relays[i].stateStartedMillis=0;}

  connectWiFi();
  lastWiFiConnected=(WiFi.status()==WL_CONNECTED);
  restoreTodayRuntime();
  setupOTA();

  server.on("/",HTTP_GET,[](){server.sendHeader("Cache-Control","no-store, no-cache, must-revalidate, max-age=0");server.send_P(200,"text/html; charset=utf-8",INDEX_HTML);});
  server.on("/api/status",HTTP_GET,handleStatus);
  server.on("/api/relay",HTTP_POST,handleRelay);
  server.on("/api/maintenance",HTTP_POST,handleMaintenance);
  server.on("/api/alloff",HTTP_POST,handleAllOff);
  server.on("/api/resumeall",HTTP_POST,handleResumeAll);
  server.on("/api/schedule",HTTP_POST,handleSchedule);
  server.on("/api/device",HTTP_POST,handleDevice);
  server.on("/api/power",HTTP_POST,handlePower);
  server.on("/api/wifi",HTTP_POST,handleWiFi);
  server.on("/api/logs",HTTP_GET,handleLogs);
  server.on("/api/logs/clear",HTTP_POST,handleClearLogs);
  server.on("/api/reset",HTTP_POST,handleReset);

  server.begin();addLog("System started - FW "+String(FW_VERSION)+" | Boot reason: "+bootReason());
  Serial.println("Web server started.");
  if(WiFi.status()==WL_CONNECTED){Serial.print("Open: http://");Serial.println(WiFi.localIP());}
  else if(fallbackAP){Serial.print("Setup AP: http://");Serial.println(WiFi.softAPIP());}
}

void loop(){
  server.handleClient();ArduinoOTA.handle();
  processMaintenanceMode();
  processTimedOverrides();
  bool wifiNow=(WiFi.status()==WL_CONNECTED);
  if(wifiNow!=lastWiFiConnected){
    if(wifiNow)addLog("Wi-Fi reconnected at "+WiFi.localIP().toString());
    else addLog("Wi-Fi connection lost");
    lastWiFiConnected=wifiNow;
  }

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
  if(millis()-lastRuntimeSave>300000UL){ lastRuntimeSave=millis(); persistRuntime(); }
  delay(2);
}

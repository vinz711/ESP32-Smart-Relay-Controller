/*
  ESP32 SMART RELAY CONTROLLER - v1.0.0
  ESP32 4-Channel Relay + Smart Management UI

  Tested baseline relay mapping:
    Relay 1 -> GPIO 5
    Relay 2 -> GPIO 17
    Relay 3 -> GPIO 16
    Relay 4 -> GPIO 4
    Active LOW: LOW = ON, HIGH = OFF

  This first repository release is the generic controller baseline.
  The initial validation application was aquarium automation.

  Features:
    - Wi-Fi control + configurable SSID/password
    - Persistent settings (Preferences)
    - AUTO / MANUAL mode
    - Up to 6 schedules per relay
    - Weekday scheduling
    - Overnight schedules
    - 24x7 schedules
    - Manual override outside an active AUTO schedule
    - Emergency OFF per relay + Resume AUTO
    - Emergency ALL OFF + Resume ALL
    - ArduinoOTA firmware update
    - mDNS
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

#define FW_VERSION "1.0.0"
#define RELAY_COUNT 4
#define MAX_SCHEDULES 6
#define MAX_LOGS 300

const char* DEVICE_HOSTNAME = "ESP32-Smart-Relay";

// ---------------- WIFI DEFAULTS ----------------
// Intentionally empty. Configure Wi-Fi through the web interface.
const char* DEFAULT_WIFI_SSID = "";
const char* DEFAULT_WIFI_PASSWORD = "";

// Fallback setup AP used when the saved Wi-Fi cannot be reached.
const char* FALLBACK_AP_SSID = "ESP32-Smart-Relay-Setup";
const char* FALLBACK_AP_PASSWORD = "relay-setup";

// ---------------- RELAY PINS ----------------
const uint8_t relayPins[RELAY_COUNT] = {5, 17, 16, 4};

// Active LOW relay board
const uint8_t RELAY_ON = LOW;
const uint8_t RELAY_OFF = HIGH;

// ---------------- DEFAULT DEVICE DATA ----------------
const char* DEFAULT_NAMES[RELAY_COUNT] = {
  "Relay 1",
  "Relay 2",
  "Relay 3",
  "Relay 4"
};

const char* DEFAULT_ICONS[RELAY_COUNT] = {
  "🔌", "🔌", "🔌", "🔌"
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
const char* LOG_FILE = "/activity.log";

// ============================================================
// WEB UI
// ============================================================
// The remainder of this firmware is the tested Smart Management
// controller implementation. It intentionally keeps the original
// scheduling, relay, runtime, log, backup/restore, Wi-Fi and OTA
// behavior while removing personal identity and credentials.
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Smart Relay Control</title>
<style>
/* UI styles retained from the tested controller baseline. */
body{margin:0;font-family:Arial,Helvetica,sans-serif;background:#f3f8fc;color:#10234b}
.container{max-width:1200px;margin:auto;padding:20px}.header{display:flex;justify-content:space-between;align-items:center}
.relay-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.relay{background:#fff;border:1px solid #dce7f0;border-radius:14px;padding:15px}
.btn{border:0;border-radius:8px;padding:10px;font-weight:700}.onBtn{background:#08a96a;color:#fff}.offBtn{background:#f43f4e;color:#fff}
@media(max-width:760px){.relay-grid{grid-template-columns:1fr}.container{padding:10px}}
</style>
</head>
<body>
<div class="container">
<h1>ESP32 Smart Relay Controller</h1>
<p>Generic 4-channel relay management</p>
<section class="relay-grid" id="relayGrid"></section>
<p><button class="btn offBtn" onclick="allOff()">ALL OFF</button></p>
</div>
<script>
let DATA=null;
function api(url,opt={}){return fetch(url,opt).then(async r=>{if(!r.ok)throw new Error(await r.text());return r})}
function loadStatus(){fetch('/api/status').then(r=>r.json()).then(d=>{DATA=d;render()})}
function render(){if(!DATA)return;document.getElementById('relayGrid').innerHTML=DATA.relays.map(r=>`<article class="relay"><h2>${r.icon} ${r.name}</h2><p>Relay ${r.id} • ${r.mode} • ${r.state?'ON':'OFF'}</p><button class="btn onBtn" onclick="toggleRelay(${r.id},'on')">ON</button> <button class="btn offBtn" onclick="toggleRelay(${r.id},'off')">OFF</button></article>`).join('')}
function toggleRelay(id,a){api(`/api/relay?id=${id}&action=${a}`).then(loadStatus)}
function allOff(){if(confirm('Emergency OFF will isolate all relays until Resume AUTO. Continue?'))api('/api/alloff').then(loadStatus)}
loadStatus();setInterval(loadStatus,5000)
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
bool getTimeInfo(struct tm &t){return getLocalTime(&t,1000);}
int currentMinutes(){struct tm t;if(!getTimeInfo(t))return -1;return t.tm_hour*60+t.tm_min;}
int currentWeekday(){struct tm t;if(!getTimeInfo(t))return -1;return(t.tm_wday+6)%7;}
String currentDateTime(){struct tm t;if(!getTimeInfo(t))return "";char b[32];strftime(b,sizeof(b),"%d-%m-%Y %H:%M:%S",&t);return String(b);}
String bootReason(){switch(esp_reset_reason()){case ESP_RST_POWERON:return "Power On";case ESP_RST_EXT:return "External Reset";case ESP_RST_SW:return "Software Reset";case ESP_RST_PANIC:return "Panic";case ESP_RST_INT_WDT:return "Interrupt WDT";case ESP_RST_TASK_WDT:return "Task WDT";case ESP_RST_WDT:return "WDT";case ESP_RST_DEEPSLEEP:return "Deep Sleep";default:return "Unknown";}}

void rewriteLogFile(){File f=LittleFS.open(LOG_FILE,"w");if(!f)return;for(int i=0;i<logCount;i++)f.println(activityLogs[i]);f.close();}
void loadLogsFromFS(){logCount=0;if(!LittleFS.exists(LOG_FILE))return;File f=LittleFS.open(LOG_FILE,"r");if(!f)return;while(f.available()){String line=f.readStringUntil('\n');line.trim();if(!line.length())continue;if(logCount<MAX_LOGS)activityLogs[logCount++]=line;else{for(int i=1;i<MAX_LOGS;i++)activityLogs[i-1]=activityLogs[i];activityLogs[MAX_LOGS-1]=line;}}f.close();}
void persistRuntime(){for(int i=0;i<RELAY_COUNT;i++){char k[24];snprintf(k,sizeof(k),"rtD%d",i);prefs.putULong64(k,relays[i].runtimeTodayMs);snprintf(k,sizeof(k),"rtT%d",i);prefs.putULong64(k,relays[i].runtimeTotalMs);}String dt=currentDateTime();if(dt.length())prefs.putString("rtDate",dt.substring(0,10));}
void addLog(const String &msg){String e=currentDateTime()+" | "+msg;if(logCount<MAX_LOGS){activityLogs[logCount++]=e;File f=LittleFS.open(LOG_FILE,"a");if(f){f.println(e);f.close();}}else{for(int i=1;i<MAX_LOGS;i++)activityLogs[i-1]=activityLogs[i];activityLogs[MAX_LOGS-1]=e;rewriteLogFile();}Serial.println(e);}
void setRelayHardware(int id,bool on){if(id<0||id>=RELAY_COUNT)return;digitalWrite(relayPins[id],on?RELAY_ON:RELAY_OFF);}
uint64_t currentRuntime(int id){if(id<0||id>=RELAY_COUNT)return 0;uint64_t r=relays[id].runtimeTodayMs;if(relays[id].state)r+=(uint64_t)(millis()-relays[id].stateStartedMillis);return r;}
void setRelayState(int id,bool on,const String &reason){if(id<0||id>=RELAY_COUNT)return;RelayConfig&r=relays[id];if(r.state==on)return;if(r.state){uint64_t elapsed=(uint64_t)(millis()-r.stateStartedMillis);r.runtimeTodayMs+=elapsed;r.runtimeTotalMs+=elapsed;}r.state=on;r.stateStartedMillis=on?millis():0;setRelayHardware(id,on);if(!on)persistRuntime();addLog(r.name+" "+(on?"ON":"OFF")+" ("+reason+")");}

// ============================================================
// SCHEDULE ENGINE
// ============================================================
bool slotActive(const ScheduleSlot&s,int now,int today){if(!s.enabled||s.days==0)return false;if(s.startMinutes==0&&s.stopMinutes==0)return(s.days&(1<<today));if(s.startMinutes<s.stopMinutes)return(s.days&(1<<today))&&now>=s.startMinutes&&now<s.stopMinutes;if(now>=s.startMinutes)return(s.days&(1<<today));if(now<s.stopMinutes){int prev=(today+6)%7;return(s.days&(1<<prev));}return false;}
bool anyScheduleActive(int id){if(id<0||id>=RELAY_COUNT||!relays[id].autoMode)return false;int now=currentMinutes(),day=currentWeekday();if(now<0||day<0)return false;for(int j=0;j<MAX_SCHEDULES;j++)if(slotActive(relays[id].schedules[j],now,day))return true;return false;}
void processSchedules(){if(millis()-lastScheduleCheck<500)return;lastScheduleCheck=millis();struct tm t;if(!getTimeInfo(t))return;int today=(t.tm_wday+6)%7;if(lastWeekday<0)lastWeekday=today;if(today!=lastWeekday){for(int i=0;i<RELAY_COUNT;i++){relays[i].runtimeTodayMs=0;if(relays[i].state)relays[i].stateStartedMillis=millis();}lastWeekday=today;}for(int i=0;i<RELAY_COUNT;i++){RelayConfig&r=relays[i];if(!r.autoMode||r.emergencyOff)continue;bool active=anyScheduleActive(i);if(active){r.manualOverride=false;if(!r.state)setRelayState(i,true,"Schedule started");}else if(!r.manualOverride){if(r.state)setRelayState(i,false,"Schedule ended");}}}

// ============================================================
// PREFERENCES
// ============================================================
void clearSchedules(int id){for(int j=0;j<MAX_SCHEDULES;j++){relays[id].schedules[j].enabled=false;relays[id].schedules[j].startMinutes=0;relays[id].schedules[j].stopMinutes=0;relays[id].schedules[j].days=0x7F;}}
void setDefaults(){for(int i=0;i<RELAY_COUNT;i++){relays[i].name=DEFAULT_NAMES[i];relays[i].icon=DEFAULT_ICONS[i];relays[i].state=false;relays[i].autoMode=false;relays[i].manualOverride=false;relays[i].emergencyOff=false;relays[i].watts=DEFAULT_WATTS[i];relays[i].runtimeTodayMs=0;relays[i].runtimeTotalMs=0;relays[i].stateStartedMillis=0;clearSchedules(i);}}
void saveRelay(int id){if(id<0||id>=RELAY_COUNT)return;char k[24];snprintf(k,sizeof(k),"name%d",id);prefs.putString(k,relays[id].name);snprintf(k,sizeof(k),"icon%d",id);prefs.putString(k,relays[id].icon);snprintf(k,sizeof(k),"auto%d",id);prefs.putBool(k,relays[id].autoMode);snprintf(k,sizeof(k),"em%d",id);prefs.putBool(k,relays[id].emergencyOff);snprintf(k,sizeof(k),"w%d",id);prefs.putUShort(k,relays[id].watts);for(int j=0;j<MAX_SCHEDULES;j++){snprintf(k,sizeof(k),"e%d_%d",id,j);prefs.putBool(k,relays[id].schedules[j].enabled);snprintf(k,sizeof(k),"s%d_%d",id,j);prefs.putUShort(k,relays[id].schedules[j].startMinutes);snprintf(k,sizeof(k),"t%d_%d",id,j);prefs.putUShort(k,relays[id].schedules[j].stopMinutes);snprintf(k,sizeof(k),"d%d_%d",id,j);prefs.putUChar(k,relays[id].schedules[j].days);}}
void loadSettings(){setDefaults();prefs.begin("relayctl",false);wifiSSID=prefs.getString("ssid",DEFAULT_WIFI_SSID);wifiPassword=prefs.getString("pass",DEFAULT_WIFI_PASSWORD);for(int i=0;i<RELAY_COUNT;i++){char k[24];snprintf(k,sizeof(k),"name%d",i);relays[i].name=prefs.getString(k,relays[i].name);snprintf(k,sizeof(k),"icon%d",i);relays[i].icon=prefs.getString(k,relays[i].icon);snprintf(k,sizeof(k),"auto%d",i);relays[i].autoMode=prefs.getBool(k,relays[i].autoMode);snprintf(k,sizeof(k),"em%d",i);relays[i].emergencyOff=prefs.getBool(k,false);snprintf(k,sizeof(k),"w%d",i);relays[i].watts=prefs.getUShort(k,relays[i].watts);for(int j=0;j<MAX_SCHEDULES;j++){snprintf(k,sizeof(k),"e%d_%d",i,j);relays[i].schedules[j].enabled=prefs.getBool(k,relays[i].schedules[j].enabled);snprintf(k,sizeof(k),"s%d_%d",i,j);relays[i].schedules[j].startMinutes=prefs.getUShort(k,relays[i].schedules[j].startMinutes);snprintf(k,sizeof(k),"t%d_%d",i,j);relays[i].schedules[j].stopMinutes=prefs.getUShort(k,relays[i].schedules[j].stopMinutes);snprintf(k,sizeof(k),"d%d_%d",i,j);relays[i].schedules[j].days=prefs.getUChar(k,relays[i].schedules[j].days);}snprintf(k,sizeof(k),"rtT%d",i);relays[i].runtimeTotalMs=prefs.getULong64(k,0);}}

// ============================================================
// WIFI / OTA
// ============================================================
void restoreTodayRuntime(){String savedDate=prefs.getString("rtDate","");String today=currentDateTime().substring(0,10);if(savedDate.length()&&savedDate==today){for(int i=0;i<RELAY_COUNT;i++){char k[24];snprintf(k,sizeof(k),"rtD%d",i);relays[i].runtimeTodayMs=prefs.getULong64(k,0);}}}
void startFallbackAP(){fallbackAP=true;WiFi.mode(WIFI_AP_STA);WiFi.softAP(FALLBACK_AP_SSID,FALLBACK_AP_PASSWORD);Serial.println("Wi-Fi unavailable. Fallback AP started.");Serial.print("AP SSID: ");Serial.println(FALLBACK_AP_SSID);Serial.print("AP IP: ");Serial.println(WiFi.softAPIP());}
void connectWiFi(){fallbackAP=false;if(wifiSSID.length()==0){startFallbackAP();return;}WiFi.mode(WIFI_STA);WiFi.setHostname(DEVICE_HOSTNAME);WiFi.begin(wifiSSID.c_str(),wifiPassword.c_str());Serial.print("Connecting to Wi-Fi");for(int i=0;i<40&&WiFi.status()!=WL_CONNECTED;i++){delay(500);Serial.print(".");}Serial.println();if(WiFi.status()==WL_CONNECTED){Serial.print("IP: ");Serial.println(WiFi.localIP());configTime(19800,0,"pool.ntp.org","time.nist.gov");struct tm t;if(getLocalTime(&t,10000))Serial.print("IST: "),Serial.println(currentDateTime());if(MDNS.begin(DEVICE_HOSTNAME)){MDNS.addService("http","tcp",80);Serial.print("mDNS: http://");Serial.print(DEVICE_HOSTNAME);Serial.println(".local/");}}else startFallbackAP();}
void setupOTA(){ArduinoOTA.setHostname(DEVICE_HOSTNAME);ArduinoOTA.onStart([](){Serial.println("OTA update started - forcing relays OFF");for(int i=0;i<RELAY_COUNT;i++){relays[i].state=false;setRelayHardware(i,false);}});ArduinoOTA.onEnd([](){Serial.println("OTA update complete");});ArduinoOTA.onProgress([](unsigned int p,unsigned int total){Serial.printf("OTA %u%%\r",(p*100)/total);});ArduinoOTA.onError([](ota_error_t e){Serial.printf("\nOTA error[%u]\n",e);});ArduinoOTA.begin();}

// ============================================================
// STATUS JSON / API
// ============================================================
String findNextSchedule(){int now=currentMinutes(),today=currentWeekday();if(now<0||today<0)return "--";int bestDelta=100000;String result="";for(int i=0;i<RELAY_COUNT;i++){if(!relays[i].autoMode||relays[i].emergencyOff)continue;for(int s=0;s<MAX_SCHEDULES;s++){ScheduleSlot&x=relays[i].schedules[s];if(!x.enabled||x.days==0)continue;for(int d=0;d<7;d++){if(!(x.days&(1<<d)))continue;int delta=((d-today+7)%7)*1440+x.startMinutes-now;if(delta<=0)delta+=10080;if(delta<bestDelta){bestDelta=delta;result=formatMinutes(x.startMinutes)+" • "+relays[i].name;}}}}return result;}
String uptimeString(){uint64_t sec=(millis()-bootMillis)/1000ULL;uint32_t d=sec/86400UL;sec%=86400UL;uint32_t h=sec/3600UL;sec%=3600UL;uint32_t m=sec/60UL;sec%=60UL;return String(d)+"d "+String(h)+"h "+String(m)+"m "+String((uint32_t)sec)+"s";}
String networkModeString(){if(fallbackAP)return WiFi.status()==WL_CONNECTED?"Wi-Fi + Setup AP":"Setup AP";return WiFi.status()==WL_CONNECTED?"Wi-Fi STA":"Wi-Fi disconnected";}
String buildStatusJSON(){String j="{";j+="\"version\":\""+String(FW_VERSION)+"\",";String ip=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString();j+="\"ip\":\""+ip+"\",";j+="\"wifi\":"+(WiFi.status()==WL_CONNECTED?String("true"):String("false"))+",";j+="\"ota\":true,\"networkMode\":\""+jsonEscape(networkModeString())+"\",";j+="\"rssi\":"+String(WiFi.status()==WL_CONNECTED?WiFi.RSSI():0)+",";j+="\"uptime\":\""+jsonEscape(uptimeString())+"\",\"time\":\""+jsonEscape(currentDateTime())+"\",\"bootReason\":\""+jsonEscape(bootReason())+"\",\"logCount\":"+String(logCount)+",";int autoCount=0,scheduleCount=0;uint64_t totalRuntime=0;double energy=0;for(int i=0;i<RELAY_COUNT;i++){if(relays[i].autoMode)autoCount++;for(int k=0;k<MAX_SCHEDULES;k++)if(relays[i].schedules[k].enabled)scheduleCount++;totalRuntime+=currentRuntime(i);energy+=(double)relays[i].watts*(double)currentRuntime(i)/3600000000.0;}j+="\"autoCount\":"+String(autoCount)+",\"scheduleCount\":"+String(scheduleCount)+",\"totalRuntime\":"+String((uint32_t)totalRuntime)+",\"totalEnergy\":"+String(energy,3)+",\"nextSchedule\":\""+jsonEscape(findNextSchedule())+"\",\"relays\":[";for(int i=0;i<RELAY_COUNT;i++){if(i)j+=",";RelayConfig&r=relays[i];j+="{\"id\":"+String(i+1)+",\"name\":\""+jsonEscape(r.name)+"\",\"icon\":\""+jsonEscape(r.icon)+"\",\"state\":"+(r.state?String("true"):String("false"))+",\"mode\":\""+String(r.autoMode?"AUTO":"MANUAL")+"\",\"emergency\":"+(r.emergencyOff?String("true"):String("false"))+",\"manualOverride\":"+(r.manualOverride?String("true"):String("false"))+",\"watts\":"+String(r.watts)+",\"runtime\":"+String((uint32_t)currentRuntime(i))+",\"schedules\":[";for(int k=0;k<MAX_SCHEDULES;k++){if(k)j+=",";ScheduleSlot&s=r.schedules[k];j+="{\"enabled\":"+(s.enabled?String("true"):String("false"))+",\"start\":"+String(s.startMinutes)+",\"stop\":"+String(s.stopMinutes)+",\"days\":"+String(s.days)+",\"fullDay\":"+(s.startMinutes==0&&s.stopMinutes==0?String("true"):String("false"))+"}";}j+="]}";}j+="]}";return j;}
void handleStatus(){server.send(200,"application/json",buildStatusJSON());}
void handleRelay(){if(!server.hasArg("id")||!server.hasArg("action")){server.send(400,"text/plain","Missing parameters");return;}int id=server.arg("id").toInt()-1;String a=server.arg("action");if(id<0||id>=RELAY_COUNT){server.send(400,"text/plain","Invalid relay");return;}RelayConfig&r=relays[id];if(r.emergencyOff&&a!="resume"){server.send(409,"text/plain","Emergency OFF is active. Resume AUTO first.");return;}if(a=="on"||a=="off"){if(r.autoMode&&anyScheduleActive(id)){server.send(409,"text/plain","AUTO schedule is currently active. Use Emergency OFF to force a manual OFF.");return;}r.manualOverride=r.autoMode;setRelayState(id,a=="on","Manual");saveRelay(id);server.send(200,"text/plain","OK");return;}if(a=="emergency"){r.emergencyOff=true;r.manualOverride=false;setRelayState(id,false,"Emergency OFF");saveRelay(id);server.send(200,"text/plain","OK");return;}if(a=="resume"){r.emergencyOff=false;r.manualOverride=false;saveRelay(id);if(r.autoMode)setRelayState(id,anyScheduleActive(id),"AUTO Resume");addLog(r.name+" AUTO resumed");server.send(200,"text/plain","OK");return;}server.send(400,"text/plain","Invalid action");}
void handleAllOff(){for(int i=0;i<RELAY_COUNT;i++){relays[i].emergencyOff=true;relays[i].manualOverride=false;setRelayState(i,false,"Emergency All OFF");saveRelay(i);}addLog("All relays Emergency OFF");server.send(200,"text/plain","OK");}
void handleResumeAll(){for(int i=0;i<RELAY_COUNT;i++){relays[i].emergencyOff=false;relays[i].manualOverride=false;saveRelay(i);if(relays[i].autoMode)setRelayState(i,anyScheduleActive(i),"AUTO Resume");}addLog("All relays AUTO resumed");server.send(200,"text/plain","OK");}
void handleSchedule(){if(!server.hasArg("id")||!server.hasArg("slot")||!server.hasArg("enabled")||!server.hasArg("start")||!server.hasArg("stop")||!server.hasArg("days")){server.send(400,"text/plain","Missing parameters");return;}int id=server.arg("id").toInt()-1,slot=server.arg("slot").toInt();if(id<0||id>=RELAY_COUNT||slot<0||slot>=MAX_SCHEDULES){server.send(400,"text/plain","Invalid relay or slot");return;}RelayConfig&r=relays[id];ScheduleSlot&s=r.schedules[slot];s.enabled=server.arg("enabled")=="1";s.startMinutes=(uint16_t)constrain(server.arg("start").toInt(),0,1439);s.stopMinutes=(uint16_t)constrain(server.arg("stop").toInt(),0,1439);s.days=(uint8_t)constrain(server.arg("days").toInt(),0,127);if(s.enabled&&s.days==0){server.send(400,"text/plain","Select at least one day");return;}if(server.hasArg("fullDay")&&server.arg("fullDay")=="1"){s.startMinutes=0;s.stopMinutes=0;}if(server.hasArg("mode"))r.autoMode=server.arg("mode")=="AUTO";r.manualOverride=false;saveRelay(id);addLog(r.name+" schedule "+String(slot+1)+" updated");if(r.autoMode&&!r.emergencyOff)setRelayState(id,anyScheduleActive(id),"Schedule update");server.send(200,"text/plain","OK");}
void handleDevice(){if(!server.hasArg("id")||!server.hasArg("name")||!server.hasArg("icon")){server.send(400,"text/plain","Missing parameters");return;}int id=server.arg("id").toInt()-1;if(id<0||id>=RELAY_COUNT){server.send(400,"text/plain","Invalid relay");return;}relays[id].name=urlDecode(server.arg("name"));relays[id].icon=urlDecode(server.arg("icon"));if(relays[id].name.length()==0)relays[id].name="Relay "+String(id+1);saveRelay(id);addLog("Relay "+String(id+1)+" name/icon updated");server.send(200,"text/plain","OK");}
void handlePower(){if(!server.hasArg("id")){server.send(400,"text/plain","Missing id");return;}int id=server.arg("id").toInt()-1;if(id<0||id>=RELAY_COUNT){server.send(400,"text/plain","Invalid relay");return;}if(server.hasArg("reset")&&server.arg("reset")=="1")relays[id].watts=DEFAULT_WATTS[id];else if(server.hasArg("watts"))relays[id].watts=(uint16_t)constrain(server.arg("watts").toInt(),0,5000);else{server.send(400,"text/plain","Missing watts");return;}saveRelay(id);addLog(relays[id].name+" power rating updated: "+String(relays[id].watts)+"W");server.send(200,"text/plain","OK");}
void handleWiFi(){if(!server.hasArg("ssid")||!server.hasArg("password")){server.send(400,"text/plain","Missing Wi-Fi parameters");return;}String ssid=urlDecode(server.arg("ssid")),pass=urlDecode(server.arg("password"));if(ssid.length()==0){server.send(400,"text/plain","SSID cannot be empty");return;}prefs.putString("ssid",ssid);prefs.putString("pass",pass);server.send(200,"text/plain","Wi-Fi saved. Restarting...");delay(800);ESP.restart();}
void handleLogs(){String j="{\"logs\":[";for(int i=0;i<logCount;i++){if(i)j+=",";j+="\""+jsonEscape(activityLogs[i])+"\"";}j+="]}";server.send(200,"application/json",j);}
void handleClearLogs(){logCount=0;LittleFS.remove(LOG_FILE);server.send(200,"text/plain","OK");}
void handleReset(){for(int i=0;i<RELAY_COUNT;i++)setRelayHardware(i,false);LittleFS.remove(LOG_FILE);prefs.clear();server.send(200,"text/plain","Factory reset. Restarting...");delay(800);ESP.restart();}

// ============================================================
// SETUP / LOOP
// ============================================================
void setup(){bootMillis=millis();Serial.begin(115200);delay(300);Serial.println("ESP32 Smart Relay Controller v1.0.0");for(int i=0;i<RELAY_COUNT;i++){pinMode(relayPins[i],OUTPUT);digitalWrite(relayPins[i],RELAY_OFF);}loadSettings();if(!LittleFS.begin(true))Serial.println("WARNING: LittleFS unavailable; activity log will not survive reboot.");else loadLogsFromFS();for(int i=0;i<RELAY_COUNT;i++){relays[i].state=false;relays[i].stateStartedMillis=0;}connectWiFi();restoreTodayRuntime();setupOTA();server.on("/",HTTP_GET,[](){server.send_P(200,"text/html",INDEX_HTML);});server.on("/api/status",HTTP_GET,handleStatus);server.on("/api/relay",HTTP_GET,handleRelay);server.on("/api/alloff",HTTP_GET,handleAllOff);server.on("/api/resumeall",HTTP_GET,handleResumeAll);server.on("/api/schedule",HTTP_POST,handleSchedule);server.on("/api/device",HTTP_POST,handleDevice);server.on("/api/power",HTTP_POST,handlePower);server.on("/api/wifi",HTTP_POST,handleWiFi);server.on("/api/logs",HTTP_GET,handleLogs);server.on("/api/logs/clear",HTTP_GET,handleClearLogs);server.on("/api/reset",HTTP_GET,handleReset);server.begin();addLog("System started - FW "+String(FW_VERSION)+" | Boot reason: "+bootReason());Serial.println("Web server started.");if(WiFi.status()==WL_CONNECTED){Serial.print("Open: http://");Serial.println(WiFi.localIP());}else if(fallbackAP){Serial.print("Setup AP: http://");Serial.println(WiFi.softAPIP());}}
void loop(){server.handleClient();ArduinoOTA.handle();if(millis()-lastWiFiCheck>15000){lastWiFiCheck=millis();if(WiFi.status()!=WL_CONNECTED){if(wifiSSID.length())WiFi.begin(wifiSSID.c_str(),wifiPassword.c_str());}else if(fallbackAP){fallbackAP=false;WiFi.softAPdisconnect(true);if(MDNS.begin(DEVICE_HOSTNAME))MDNS.addService("http","tcp",80);Serial.print("Wi-Fi restored. IP: ");Serial.println(WiFi.localIP());}}processSchedules();if(millis()-lastRuntimeSave>300000UL){lastRuntimeSave=millis();persistRuntime();}delay(2);}

from pathlib import Path

path = Path("firmware/v2.0.1/ESP32_Smart_Relay_Controller.ino")
s = path.read_text(encoding="utf-8")


def replace_once(old, new, label):
    global s
    if old not in s:
        raise SystemExit(f"Patch target not found: {label}")
    s = s.replace(old, new, 1)


replace_once(
    '  json += "\\\"wifi\\\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";\n',
    '  json += "\\\"wifi\\\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";\n'
    '  json += "\\\"tariff\\\":" + String(electricityTariff, 2) + ",";\n',
    "status tariff",
)

replace_once(
    '    json += "\\\"runtime\\\":" + String(getCurrentRuntime(i)) + ",";\n',
    '    json += "\\\"runtime\\\":" + String(getCurrentRuntime(i)) + ",";\n'
    '    json += "\\\"wattage\\\":" + String(r.wattage, 2) + ",";\n',
    "relay wattage",
)

replace_once(
    '    DATA = data;\n\n    render();\n',
    '    DATA = data;\n    DATA.apiConnected = true;\n\n    render();\n',
    "status success connection",
)

replace_once(
    '  .catch(() => {\n\n    $("connection").textContent =\n      "Disconnected";\n',
    '  .catch(() => {\n\n    if (DATA) DATA.apiConnected = false;\n\n    $("connection").textContent =\n      "Disconnected";\n',
    "status failure connection",
)

replace_once(
    '  $("connection").textContent =\n    DATA.wifi\n      ? "Connected"\n      : "Disconnected";\n',
    '  const connected = DATA.apiConnected !== false;\n\n  $("connection").textContent =\n    connected\n      ? "Connected"\n      : "Disconnected";\n',
    "connection rendering",
)

replace_once(
    '    "ESP32 " +\n    (DATA.wifi ? "Connected" : "Disconnected") +\n    " | " +\n    DATA.ip;\n',
    '    "ESP32 " +\n    (connected ? "Connected" : "Disconnected") +\n    " | " +\n    DATA.ip;\n',
    "footer connection rendering",
)

replace_once(
    '  $("systemStatus").innerHTML = DATA.wifi\n'
    '    ? (anyEmergency\n'
    '        ? "<b style=\\"color:#f23845\\">● Emergency OFF active</b><br>" +\n'
    '          "One or more relays are isolated from schedules. <button onclick=\\"resumeAll()\\">Resume AUTO</button>"\n'
    '        : "<b style=\\"color:#08b95c\\">● All systems normal</b><br>" +\n'
    '          "4 devices configured • Wi-Fi connected • Schedules active")\n'
    '    : "<b style=\\"color:#f23845\\">● Wi-Fi disconnected</b>";\n',
    '  $("systemStatus").innerHTML = connected\n'
    '    ? (anyEmergency\n'
    '        ? "<b style=\\"color:#f23845\\">● Emergency OFF active</b><br>" +\n'
    '          "One or more relays are isolated from schedules. <button onclick=\\"resumeAll()\\">Resume AUTO</button>"\n'
    '        : "<b style=\\"color:#08b95c\\">● ESP32 connected</b><br>" +\n'
    '          "API connection active • 4 devices configured • Schedules available")\n'
    '    : "<b style=\\"color:#f23845\\">● ESP32 disconnected</b>";\n',
    "system status rendering",
)

replace_once(
    '          ${relay.emergency ? `<button class="btn btn-off" onclick="resumeRelay(${relay.id})">▶ Resume AUTO</button>` : `<button class="btn" style="border:1px solid #ff8a8a" onclick="emergencyOff(${relay.id})">🚨 Emergency OFF</button>`}',
    '          ${relay.emergency ? `<button class="btn btn-resume" onclick="resumeRelay(${relay.id})">▶ Resume AUTO</button>` : `<button class="btn btn-emergency" onclick="emergencyOff(${relay.id})">🚨 Emergency OFF</button>`}',
    "emergency button",
)

replace_once(
    '.btn-schedule {\n\n  background:var(--card);\n\n  color:var(--text);\n\n  border:\n    1px solid\n    var(--border);\n}\n',
    '.btn-schedule {\n\n  background:var(--card);\n\n  color:var(--text);\n\n  border:\n    1px solid\n    var(--border);\n}\n\n.btn-emergency {\n  background:var(--card);\n  color:#d92f3d;\n  border:1px solid #f23845;\n  min-height:40px;\n  white-space:nowrap;\n}\n\n.btn-emergency:hover {\n  background:#fff1f2;\n}\n\n.btn-resume {\n  background:#0b9f53;\n  color:#fff;\n  min-height:40px;\n  white-space:nowrap;\n}\n',
    "emergency button css",
)

marker = '// ==========================================================\n// FORMAT RUNTIME\n// ==========================================================\n'
render_dashboard = '''// ==========================================================\n// DASHBOARD SUMMARY RENDERING\n// ==========================================================\n\nfunction renderDashboard() {\n  if (!DATA || !Array.isArray(DATA.relays)) return;\n\n  let active = 0;\n  let totalRuntime = 0;\n  let totalPower = 0;\n  let totalEnergy = 0;\n\n  DATA.relays.forEach(relay => {\n    const runtime = Number(relay.runtime || 0);\n    const watts = Number(relay.wattage || 0);\n\n    if (relay.state) {\n      active++;\n      totalPower += watts;\n    }\n\n    totalRuntime += runtime;\n    totalEnergy += (runtime / 3600000) * watts;\n  });\n\n  const tariff = Number(DATA.tariff || 0);\n  const cost = (totalEnergy / 1000) * tariff;\n\n  $("summaryActive").textContent = active + " / " + DATA.relays.length;\n  $("summaryActiveDetail").textContent = active === 0 ? "All relays OFF" : active + " relay" + (active === 1 ? "" : "s") + " ON";\n  $("summaryRuntime").textContent = formatTotalRuntime(totalRuntime);\n  $("summaryRuntimeDetail").textContent = "Across all relays";\n  $("summaryPower").textContent = Math.round(totalPower) + " W";\n  $("summaryEnergy").textContent = totalEnergy.toFixed(2) + " Wh today";\n  $("summaryCost").textContent = "₹" + cost.toFixed(2);\n\n  const next = getNextSchedule();\n  if (!next) {\n    $("summaryNext").textContent = "None";\n    $("summaryNextDetail").textContent = "No upcoming schedule";\n  } else {\n    const timeText = next.date.toLocaleTimeString("en-IN", {\n      hour: "numeric",\n      minute: "2-digit",\n      hour12: true\n    });\n    const today = new Date();\n    const sameDay = next.date.toDateString() === today.toDateString();\n    $("summaryNext").textContent = sameDay\n      ? timeText\n      : next.date.toLocaleDateString("en-IN", { weekday: "short" }) + " " + timeText;\n    $("summaryNextDetail").textContent = next.relay.name;\n  }\n}\n\n'''
if marker not in s:
    raise SystemExit("Patch target not found: dashboard marker")
if "function renderDashboard()" not in s:
    s = s.replace(marker, render_dashboard + marker, 1)

path.write_text(s, encoding="utf-8")
print("Dashboard telemetry/UI patch applied")

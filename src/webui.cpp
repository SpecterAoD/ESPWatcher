#include "webui.h"

#include <ESP8266WebServer.h>

#include "health_eval.h"
#include "ota_mgr.h"

namespace {
ESP8266WebServer server(80);
AppState* s = nullptr;

String ringToJson(const RingBuffer& buf) {
  String out = "[";
  for (uint8_t i = 0; i < buf.count; ++i) {
    uint8_t idx = (buf.head + HISTORY_SIZE - buf.count + i) % HISTORY_SIZE;
    if (i) out += ',';
    if (isnan(buf.values[idx])) out += "null";
    else out += String(buf.values[idx], 1);
  }
  out += "]";
  return out;
}

String pageHeader(const char* title) {
  String html;
  html += F("<!DOCTYPE html><html lang='de'><head>"
            "<meta charset='UTF-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += "<title>ESPWatcher - ";
  html += title;
  html += F("</title>"
            "<script src='https://cdn.tailwindcss.com'></script>"
            "<script>tailwind.config={darkMode:'class'}</script>"
            "<style>.GREEN{color:#22c55e}.YELLOW{color:#eab308}.RED{color:#ef4444}</style>"
            // Prevent flash of wrong theme
            "<script>"
            "if(localStorage.getItem('dark')==='1'||"
            "(!localStorage.getItem('dark')&&window.matchMedia('(prefers-color-scheme:dark)').matches)){"
            "document.documentElement.classList.add('dark')}"
            "</script>"
            "</head>"
            "<body class='bg-gray-100 dark:bg-gray-900 text-gray-800 dark:text-gray-100 min-h-screen'>"
            "<div class='max-w-2xl mx-auto px-4 py-4'>"
            "<div class='flex items-center justify-between mb-4'>"
            "<nav class='flex gap-4'>"
            "<a href='/' class='font-semibold hover:text-blue-500 dark:hover:text-blue-400'>Dashboard</a>"
            "<a href='/charts' class='font-semibold hover:text-blue-500 dark:hover:text-blue-400'>Charts</a>"
            "<a href='/links' class='font-semibold hover:text-blue-500 dark:hover:text-blue-400'>Links</a>"
            "</nav>"
            "<button onclick=\"var d=document.documentElement;"
            "d.classList.toggle('dark');"
            "localStorage.setItem('dark',d.classList.contains('dark')?'1':'0');\""
            " class='px-3 py-1 rounded bg-gray-300 dark:bg-gray-700 text-sm hover:opacity-80'>&#127769; Dark</button>"
            "</div><hr class='border-gray-300 dark:border-gray-700 mb-4'>");
  html += "<h2 class='text-2xl font-bold mb-4'>";
  html += title;
  html += "</h2>";
  return html;
}

String pageFooter() {
  return F("</div></body></html>");
}

void handleApiState() {
  String json = "{";
  json += F("\"cpu\":");
  if (isnan(s->latest.cpuUsage)) json += F("null"); else json += String(s->latest.cpuUsage, 1);
  json += F(",\"ram\":");
  if (isnan(s->latest.ramUsage)) json += F("null"); else json += String(s->latest.ramUsage, 1);
  json += F(",\"temp\":");
  if (isnan(s->latest.temperature)) json += F("null"); else json += String(s->latest.temperature, 1);
  json += F(",\"load1\":");
  if (isnan(s->latest.load1)) json += F("null"); else json += String(s->latest.load1, 2);
  json += F(",\"health\":\"");
  json += healthToText(s->systemHealth);
  json += F("\",\"alerts\":\"");
  json += s->latest.alertSummary;
  json += F("\",\"netdata\":");
  json += s->latest.netdataReachable ? F("true") : F("false");
  json += F(",\"throttled\":");
  json += s->latest.throttled ? F("true") : F("false");
  json += F(",\"throttledAvailable\":");
  json += s->latest.throttledAvailable ? F("true") : F("false");
  json += "}";
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.send(200, F("application/json"), json);
}

void handleApiHistory() {
  String json = "{\"cpu\":";
  json += ringToJson(s->cpuHistory);
  json += ",\"ram\":";
  json += ringToJson(s->ramHistory);
  json += ",\"temp\":";
  json += ringToJson(s->tempHistory);
  json += "}";
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.send(200, F("application/json"), json);
}

void handleDashboard() {
  String html = pageHeader("Dashboard");

  // Netdata status
  bool nd = s->latest.netdataReachable;
  html += nd
    ? F("<div class='mb-4 p-2 rounded text-sm bg-green-100 dark:bg-green-900 text-green-700 dark:text-green-300'>&#10003; Netdata erreichbar</div>")
    : F("<div class='mb-4 p-2 rounded text-sm bg-red-100 dark:bg-red-900 text-red-700 dark:text-red-300'>&#10007; Netdata nicht erreichbar</div>");

  // Metric cards
  html += F("<div class='grid grid-cols-2 gap-4 mb-4'>");

  // CPU
  html += F("<div class='bg-white dark:bg-gray-800 rounded-lg p-4 shadow'>"
            "<div class='text-xs text-gray-500 dark:text-gray-400 mb-1 uppercase tracking-wide'>CPU</div>"
            "<div id='cpu-val' class='text-3xl font-bold'>");
  html += isnan(s->latest.cpuUsage) ? String("--") : String(s->latest.cpuUsage, 1);
  html += F("<span class='text-lg font-normal'>%</span></div>"
            "<div class='mt-2 h-2 bg-gray-200 dark:bg-gray-700 rounded-full'>"
            "<div id='cpu-bar' class='h-2 bg-blue-500 rounded-full transition-all' style='width:");
  html += isnan(s->latest.cpuUsage) ? String("0") : String(static_cast<int>(s->latest.cpuUsage));
  html += F("%'></div></div></div>");

  // RAM
  html += F("<div class='bg-white dark:bg-gray-800 rounded-lg p-4 shadow'>"
            "<div class='text-xs text-gray-500 dark:text-gray-400 mb-1 uppercase tracking-wide'>RAM</div>"
            "<div id='ram-val' class='text-3xl font-bold'>");
  html += isnan(s->latest.ramUsage) ? String("--") : String(s->latest.ramUsage, 1);
  html += F("<span class='text-lg font-normal'>%</span></div>"
            "<div class='mt-2 h-2 bg-gray-200 dark:bg-gray-700 rounded-full'>"
            "<div id='ram-bar' class='h-2 bg-purple-500 rounded-full transition-all' style='width:");
  html += isnan(s->latest.ramUsage) ? String("0") : String(static_cast<int>(s->latest.ramUsage));
  html += F("%'></div></div></div>");

  // Temperature
  html += F("<div class='bg-white dark:bg-gray-800 rounded-lg p-4 shadow'>"
            "<div class='text-xs text-gray-500 dark:text-gray-400 mb-1 uppercase tracking-wide'>Temperatur</div>"
            "<div id='temp-val' class='text-3xl font-bold'>");
  html += isnan(s->latest.temperature) ? String("--") : String(s->latest.temperature, 1);
  html += F("<span class='text-lg font-normal'>&#176;C</span></div></div>");

  // Load
  html += F("<div class='bg-white dark:bg-gray-800 rounded-lg p-4 shadow'>"
            "<div class='text-xs text-gray-500 dark:text-gray-400 mb-1 uppercase tracking-wide'>Load (1min)</div>"
            "<div id='load-val' class='text-3xl font-bold'>");
  html += isnan(s->latest.load1) ? String("--") : String(s->latest.load1, 2);
  html += F("</div></div></div>"); // end grid

  // Health & Alerts
  html += F("<div class='bg-white dark:bg-gray-800 rounded-lg p-4 shadow mb-4'>"
            "<div class='text-xs text-gray-500 dark:text-gray-400 mb-1 uppercase tracking-wide'>System-Status</div>"
            "<div class='text-xl font-semibold ");
  html += healthToText(s->systemHealth);
  html += "'>";
  html += healthToText(s->systemHealth);
  html += F("</div><div class='mt-2 text-sm'>Alerts: ");
  html += s->latest.alertSummary;
  if (s->latest.throttledAvailable) {
    html += F("</div><div class='mt-1 text-sm'>Throttle: ");
    html += s->latest.throttled ? F("&#9888; Aktiv") : F("&#10003; Klar");
  }
  html += F("</div></div>");

  // OTA
  html += F("<div class='bg-white dark:bg-gray-800 rounded-lg p-4 shadow mb-4'>"
            "<div class='text-xs text-gray-500 dark:text-gray-400 mb-1 uppercase tracking-wide'>OTA Updates</div>"
            "<div class='text-sm'>Status: ");
  html += s->otaStatus;
  html += F("</div><div class='text-sm'>Auto-Update: ");
  html += otaAutoEnabled(*s) ? F("An") : F("Aus");
  html += F("</div><div class='mt-2 flex gap-2'>"
            "<a href='/ota?auto=1' class='px-3 py-1 bg-blue-500 hover:bg-blue-600 text-white rounded text-sm'>Aktivieren</a>"
            "<a href='/ota?auto=0' class='px-3 py-1 bg-gray-500 hover:bg-gray-600 text-white rounded text-sm'>Deaktivieren</a>"
            "</div></div>");

  // Auto-refresh JS
  html += F("<script>"
            "setInterval(function(){"
            "fetch('/api/state').then(function(r){return r.json();}).then(function(d){"
            "var cv=document.getElementById('cpu-val');"
            "var cb=document.getElementById('cpu-bar');"
            "var rv=document.getElementById('ram-val');"
            "var rb=document.getElementById('ram-bar');"
            "var tv=document.getElementById('temp-val');"
            "var lv=document.getElementById('load-val');"
            "if(cv)cv.innerHTML=(d.cpu===null?'--':parseFloat(d.cpu).toFixed(1))+'<span class=\"text-lg font-normal\">%</span>';"
            "if(cb)cb.style.width=(d.cpu===null?0:Math.min(d.cpu,100))+'%';"
            "if(rv)rv.innerHTML=(d.ram===null?'--':parseFloat(d.ram).toFixed(1))+'<span class=\"text-lg font-normal\">%</span>';"
            "if(rb)rb.style.width=(d.ram===null?0:Math.min(d.ram,100))+'%';"
            "if(tv)tv.innerHTML=(d.temp===null?'--':parseFloat(d.temp).toFixed(1))+'<span class=\"text-lg font-normal\">&#176;C</span>';"
            "if(lv)lv.innerHTML=(d.load1===null?'--':parseFloat(d.load1).toFixed(2));"
            "});},5000);"
            "</script>");

  html += pageFooter();
  server.send(200, F("text/html"), html);
}

void handleCharts() {
  String html = pageHeader("Charts");
  html += F("<script src='https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js'></script>");

  // CPU chart
  html += F("<div class='bg-white dark:bg-gray-800 rounded-lg p-4 shadow mb-4'>"
            "<div class='text-sm font-semibold mb-2 text-gray-700 dark:text-gray-200'>CPU-Verlauf (%)</div>"
            "<canvas id='cpuChart' height='120'></canvas></div>");

  // RAM chart
  html += F("<div class='bg-white dark:bg-gray-800 rounded-lg p-4 shadow mb-4'>"
            "<div class='text-sm font-semibold mb-2 text-gray-700 dark:text-gray-200'>RAM-Verlauf (%)</div>"
            "<canvas id='ramChart' height='120'></canvas></div>");

  // Temperature chart
  html += F("<div class='bg-white dark:bg-gray-800 rounded-lg p-4 shadow mb-4'>"
            "<div class='text-sm font-semibold mb-2 text-gray-700 dark:text-gray-200'>Temperatur-Verlauf (&#176;C)</div>"
            "<canvas id='tempChart' height='120'></canvas></div>");

  // Embed current history as initial data
  html += "<script>var _cpu=";
  html += ringToJson(s->cpuHistory);
  html += ";var _ram=";
  html += ringToJson(s->ramHistory);
  html += ";var _temp=";
  html += ringToJson(s->tempHistory);
  html += ";</script>";

  // Chart.js setup + auto-refresh
  html += F("<script>"
            "function isDark(){return document.documentElement.classList.contains('dark');}"
            "function gridColor(){return isDark()?'rgba(255,255,255,0.1)':'rgba(0,0,0,0.08)';}"
            "function textColor(){return isDark()?'#d1d5db':'#374151';}"
            "function makeChart(id,data,color,yMax){"
            "var ctx=document.getElementById(id).getContext('2d');"
            "return new Chart(ctx,{type:'line',"
            "data:{labels:data.map(function(_,i){return i;}),"
            "datasets:[{data:data,borderColor:color,backgroundColor:color.replace('1)','0.15)'),tension:0.35,fill:true,pointRadius:0,borderWidth:2}]},"
            "options:{animation:false,responsive:true,"
            "scales:{"
            "y:{beginAtZero:true,max:yMax,grid:{color:gridColor()},ticks:{color:textColor()}},"
            "x:{display:false}},"
            "plugins:{legend:{display:false}}}});"
            "}"
            "var cpuC=makeChart('cpuChart',_cpu,'rgba(59,130,246,1)',100);"
            "var ramC=makeChart('ramChart',_ram,'rgba(168,85,247,1)',100);"
            "var tmpC=makeChart('tempChart',_temp,'rgba(249,115,22,1)',undefined);"
            "setInterval(function(){"
            "fetch('/api/history').then(function(r){return r.json();}).then(function(h){"
            "function upd(c,d){c.data.datasets[0].data=d;c.data.labels=d.map(function(_,i){return i;});c.update('none');}"
            "upd(cpuC,h.cpu);upd(ramC,h.ram);upd(tmpC,h.temp);"
            "});},5000);"
            "</script>");

  html += pageFooter();
  server.send(200, F("text/html"), html);
}

void handleLinks() {
  String html = pageHeader("Links");
  html += F("<div class='bg-white dark:bg-gray-800 rounded-lg p-4 shadow'>"
            "<ul class='space-y-3'>"
            "<li><a href='http://192.168.2.6:19999' target='_blank' class='text-blue-500 hover:underline'>&#128279; Netdata</a></li>"
            "<li><a href='http://192.168.2.6/admin' target='_blank' class='text-blue-500 hover:underline'>&#128279; Pi-hole</a></li>"
            "<li><a href='http://192.168.2.6:8083' target='_blank' class='text-blue-500 hover:underline'>&#128279; dnsdist</a></li>"
            "<li><a href='http://192.168.2.6:20211' target='_blank' class='text-blue-500 hover:underline'>&#128279; NetAlertX</a></li>"
            "</ul></div>");
  html += pageFooter();
  server.send(200, F("text/html"), html);
}

void handleOtaToggle() {
  if (server.hasArg("auto")) {
    otaSetAuto(*s, server.arg("auto") == "1");
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}
}  // namespace

void webSetup(AppState& state) {
  s = &state;
  server.on("/", handleDashboard);
  server.on("/charts", handleCharts);
  server.on("/links", handleLinks);
  server.on("/ota", handleOtaToggle);
  server.on("/api/state", handleApiState);
  server.on("/api/history", handleApiHistory);
  server.begin();
}

void webLoop() {
  server.handleClient();
}

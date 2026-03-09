#include "webui.h"

#include <ESP8266WebServer.h>

#include "health_eval.h"
#include "ota_mgr.h"

namespace {
ESP8266WebServer server(80);
AppState* s = nullptr;

String ringToCsv(const RingBuffer& buf) {
  String out;
  for (uint8_t i = 0; i < buf.count; ++i) {
    uint8_t idx = (buf.head + HISTORY_SIZE - buf.count + i) % HISTORY_SIZE;
    if (i) out += ',';
    out += String(buf.values[idx], 1);
  }
  return out;
}

String pageHeader(const char* title) {
  String html;
  html += "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:sans-serif;margin:16px;}nav a{margin-right:10px;}";
  html += ".GREEN{color:#0a0}.YELLOW{color:#d90}.RED{color:#d00}";
  html += ".ok{color:#0a0;font-weight:bold}.bad{color:#d00;font-weight:bold}.warn{color:#d90;font-weight:bold}";
  html += "table{border-collapse:collapse}td{padding:4px 8px;border-bottom:1px solid #ddd}</style></head><body>";
  html += "<nav><a href='/'>Dashboard</a><a href='/charts'>Charts</a><a href='/links'>Links</a></nav><hr>";
  html += "<h2>";
  html += title;
  html += "</h2>";
  return html;
}

const char* yesNo(bool v) { return v ? "yes" : "no"; }

String otaClass() {
  if (s->otaStatus.indexOf("failed") >= 0 || s->otaStatus.indexOf("Failed") >= 0) return "bad";
  if (s->otaStatus.indexOf("New") >= 0 || s->otaStatus.indexOf("checking") >= 0 || s->otaStatus.indexOf("downloading") >= 0) return "warn";
  return "ok";
}

void handleDashboard() {
  String html = pageHeader("Dashboard");
  html += "<table>";
  html += "<tr><td>Firmware local</td><td>" + s->localFwVersion + "</td></tr>";
  html += "<tr><td>Firmware remote</td><td>" + s->remoteFwVersion + "</td></tr>";
  html += "<tr><td>Update available</td><td>" + String(yesNo(s->otaUpdateAvailable)) + "</td></tr>";
  html += "<tr><td>OTA status</td><td class='" + otaClass() + "'>" + s->otaStatus + "</td></tr>";
  html += "<tr><td>Time</td><td>" + s->currentTime + "</td></tr>";
  html += "<tr><td>DNS</td><td>" + s->dnsServer + "</td></tr>";
  html += "<tr><td>ESP IP</td><td>" + s->deviceIp + "</td></tr>";
  html += "<tr><td>Auto OTA</td><td>" + String(otaAutoEnabled(*s) ? "on" : "off") + "</td></tr>";
  html += "</table>";

  if (s->otaLastError.length()) {
    html += "<p class='bad'>OTA error: " + s->otaLastError + "</p>";
  }

  html += "<hr>";
  html += "<p>System state: <b class='" + String(healthToText(s->systemHealth)) + "'>" + healthToText(s->systemHealth) + "</b></p>";
  html += "<p>CPU: " + String(s->latest.cpuUsage, 1) + "%</p>";
  html += "<p>RAM: " + String(s->latest.ramUsage, 1) + "%</p>";
  html += "<p>Temperature: " + String(s->latest.temperature, 1) + " C</p>";
  html += "<p>Load1: " + String(s->latest.load1, 2) + "</p>";
  html += "<p>Alerts: " + s->latest.alertSummary + "</p>";
  html += "<p>Unavailable via Netdata: " + s->latest.unavailableMetrics + "</p>";
  html += "<p><a href='/ota?auto=1'>Enable auto OTA</a> | <a href='/ota?auto=0'>Disable auto OTA</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleCharts() {
  String html = pageHeader("Charts");
  html += "<p>CPU history (%): " + ringToCsv(s->cpuHistory) + "</p>";
  html += "<p>RAM history (%): " + ringToCsv(s->ramHistory) + "</p>";
  html += "<p>Temp history (C): " + ringToCsv(s->tempHistory) + "</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleLinks() {
  String html = pageHeader("Links");
  html += "<ul>";
  html += "<li><a href='http://192.168.2.6:19999' target='_blank'>Netdata</a></li>";
  html += "<li><a href='http://192.168.2.6/admin' target='_blank'>Pi-hole</a></li>";
  html += "<li><a href='http://192.168.2.6:8083' target='_blank'>dnsdist</a></li>";
  html += "<li><a href='http://192.168.2.6:20211' target='_blank'>NetAlertX</a></li>";
  html += "</ul></body></html>";
  server.send(200, "text/html", html);
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
  server.begin();
}

void webLoop() {
  server.handleClient();
}

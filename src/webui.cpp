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
  html += ".GREEN{color:#0a0}.YELLOW{color:#d90}.RED{color:#d00}</style></head><body>";
  html += "<nav><a href='/'>Dashboard</a><a href='/charts'>Charts</a><a href='/links'>Links</a></nav><hr>";
  html += "<h2>";
  html += title;
  html += "</h2>";
  return html;
}

void handleDashboard() {
  String html = pageHeader("Dashboard");
  html += "<p>CPU: " + String(s->latest.cpuUsage, 1) + "%</p>";
  html += "<p>RAM: " + String(s->latest.ramUsage, 1) + "%</p>";
  html += "<p>Temperature: " + String(s->latest.temperature, 1) + " C</p>";
  html += "<p>Load1: " + String(s->latest.load1, 2) + "</p>";
  html += "<p>System state: <b class='" + String(healthToText(s->systemHealth)) + "'>" + healthToText(s->systemHealth) + "</b></p>";
  html += "<p>Alerts: " + s->latest.alertSummary + "</p>";
  html += "<p>Throttle: ";
  if (s->latest.throttledAvailable) {
    html += s->latest.throttled ? "active" : "clear";
  } else {
    html += "unavailable";
  }
  html += "</p>";
  html += "<p>Unavailable via Netdata: " + s->latest.unavailableMetrics + "</p>";
  html += "<p>OTA status: " + s->otaStatus + "</p>";
  html += "<p>Auto OTA: " + String(otaAutoEnabled(*s) ? "on" : "off") + "</p>";
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

#include "webui.h"

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "config.h"
#include "health_eval.h"
#include "ota_mgr.h"
#include "status_client.h"

#define STR_(x) #x
#define STR(x) STR_(x)

namespace {
ESP8266WebServer server(80);
AppState* s = nullptr;

const char CSS[] PROGMEM =
    "<style>"
    ":root{color-scheme:dark}"
    "body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;margin:0;background:#101418;color:#e6e9ec}"
    "main{max-width:880px;margin:0 auto;padding:12px}"
    "nav{display:flex;gap:14px;padding:10px 12px;background:#161c22;border-bottom:1px solid #2a323b}"
    "nav a{color:#9fb4c7;text-decoration:none;font-weight:600}"
    "h2{font-size:15px;color:#9fb4c7;margin:18px 0 8px;text-transform:uppercase;letter-spacing:.06em}"
    ".hero{padding:16px;border-radius:10px;border-left:10px solid #666;background:#1b222a}"
    ".hero b{font-size:22px}.hero div{margin-top:4px;color:#c3ccd5}"
    ".GREEN{border-color:#2ecc71}.YELLOW{border-color:#f1c40f}.RED{border-color:#e74c3c}"
    ".banner{margin-top:8px;padding:10px 14px;border-radius:8px;background:#1b222a;border-left:6px solid #3498db}"
    ".tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(135px,1fr));gap:8px}"
    ".tile{padding:10px;border-radius:8px;background:#1b222a;border-left:6px solid #5b6672}"
    ".tile small{display:block;color:#93a1af;font-size:12px}.tile span{font-size:17px;font-weight:600}"
    ".ok{border-color:#2ecc71}.warn{border-color:#f1c40f}.bad{border-color:#e74c3c}"
    "table{border-collapse:collapse;width:100%}td{padding:5px 8px;border-bottom:1px solid #2a323b;font-size:14px}"
    "td:first-child{color:#93a1af;width:42%}"
    "a{color:#6cb2eb}svg{width:100%;height:70px;background:#1b222a;border-radius:8px}"
    ".note{color:#f1c40f}"
    "</style>";

String esc(const char* in) {
  String out;
  for (const char* p = in; *p; ++p) {
    switch (*p) {
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '&': out += F("&amp;"); break;
      case '\'': out += F("&#39;"); break;
      case '"': out += F("&quot;"); break;
      default: out += *p; break;
    }
  }
  return out;
}

String esc(const String& in) { return esc(in.c_str()); }

String fmt(float v, uint8_t digits, const char* unit) {
  if (isnan(v)) return String(F("–"));
  return String(v, digits) + unit;
}

void flush(String& buf) {
  if (buf.length()) {
    server.sendContent(buf);
    buf = "";
  }
}

void beginPage(bool autoRefresh) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, F("text/html; charset=utf-8"), "");
  String head;
  head += F("<!doctype html><html lang='de'><head><meta charset='utf-8'>");
  head += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  head += F("<title>ESPWatcher</title>");
  if (autoRefresh) head += F("<meta http-equiv='refresh' content='10'>");
  flush(head);
  server.sendContent_P(CSS);
  head += F("</head><body><nav><a href='/'>Übersicht</a><a href='/charts'>Verlauf</a><a href='/links'>Links</a></nav><main>");
  flush(head);
}

void endPage() {
  server.sendContent_P(PSTR("</main></body></html>"));
  server.sendContent("");
}

void tile(String& b, const char* cls, const char* title, const String& value) {
  b += F("<div class='tile ");
  b += cls;
  b += F("'><small>");
  b += title;
  b += F("</small><span>");
  b += value;
  b += F("</span></div>");
}

const char* svcClass(const ServiceEntry& e) {
  switch (e.state) {
    case SvcState::ACTIVE: return "ok";
    case SvcState::FAILED: return "bad";
    case SvcState::INACTIVE: return e.critical ? "bad" : "warn";
    default: return "";
  }
}

const char* checkClass(int8_t v) { return v == 1 ? "ok" : (v == 0 ? "bad" : ""); }
const char* checkText(int8_t v) { return v == 1 ? "antwortet" : (v == 0 ? "keine Antwort" : "unbekannt"); }

const char* thresholdClass(float v, float warn, float bad) {
  if (isnan(v)) return "";
  if (v >= bad) return "bad";
  if (v >= warn) return "warn";
  return "ok";
}

String ageText(unsigned long sinceMs) {
  unsigned long sec = (millis() - sinceMs) / 1000UL;
  if (sec < 90) return String(sec) + " s";
  return String(sec / 60UL) + " min";
}

void sendDashboard() {
  const StatusPoint& st = s->status;
  const MetricPoint& m = s->latest;

  beginPage(true);
  String b;

  // --- Gesamtstatus -------------------------------------------------------
  b += F("<div class='hero ");
  b += healthToText(s->systemHealth);
  b += F("'><b>");
  b += healthToGerman(s->systemHealth);
  b += F("</b><div>");
  b += esc(s->healthReason);
  b += F("</div></div>");

  if (st.activeDns != "unknown") {
    b += F("<div class='banner'>Aktiver DNS: <b>");
    b += esc(st.activeDns);
    b += F("</b> (" DNS_VIP ")</div>");
  }
  if (!st.reachable) {
    b += F("<div class='banner note'>Status-JSON nicht erreichbar");
    if (st.timestampMs) {
      b += F(" – letzte Daten vor ");
      b += ageText(st.timestampMs);
    }
    b += F("</div>");
  }
  flush(b);

  // --- Dienste ------------------------------------------------------------
  if (st.serviceCount) {
    b += F("<h2>Dienste auf piholepi</h2><div class='tiles'>");
    for (uint8_t i = 0; i < st.serviceCount; ++i) {
      const ServiceEntry& e = st.services[i];
      tile(b, svcClass(e), esc(e.name).c_str(), svcStateToText(e.state));
    }
    b += F("</div>");
    flush(b);
  }

  // --- DNS-Pruefungen -----------------------------------------------------
  if (st.reachable) {
    b += F("<h2>DNS-Erreichbarkeit</h2><div class='tiles'>");
    tile(b, checkClass(st.dnsLocal), "piholepi", checkText(st.dnsLocal));
    tile(b, checkClass(st.dnsVip), "VIP " DNS_VIP, checkText(st.dnsVip));
    tile(b, checkClass(st.dnsPizero), "pizero", checkText(st.dnsPizero));
    b += F("</div>");
    flush(b);
  }

  // --- Messwerte ----------------------------------------------------------
  b += F("<h2>System piholepi</h2><div class='tiles'>");
  tile(b, thresholdClass(m.cpuUsage, 80, 95), "CPU", fmt(m.cpuUsage, 0, " %"));
  tile(b, thresholdClass(m.ramUsage, 85, 95), "RAM", fmt(m.ramUsage, 0, " %"));
  tile(b, thresholdClass(m.temperature, 70, 80), "Temperatur", fmt(m.temperature, 1, " °C"));
  tile(b, "", "Load (1 min)", fmt(m.load1, 2, ""));
  if (st.diskMounted == 0) {
    tile(b, "bad", "Datenplatte", String(F("nicht eingehängt")));
  } else {
    tile(b, thresholdClass(st.diskUsedPct, 85, 95), "Datenplatte", fmt(st.diskUsedPct, 0, " % voll"));
  }
  tile(b, thresholdClass(st.backupAgeH, 30, 36), "Letztes Backup", fmt(st.backupAgeH, 0, " h her"));
  if (st.throttled >= 0) {
    tile(b, st.throttled ? "bad" : "ok", "Pi-Drosselung", st.throttled ? String(F("aktiv")) : String(F("keine")));
  }
  tile(b, m.criticalAlerts ? "bad" : (m.warningAlerts ? "warn" : (m.netdataReachable ? "ok" : "")), "Netdata-Alarme",
       m.netdataReachable ? String(m.criticalAlerts) + " krit. / " + String(m.warningAlerts) + " warn." : String(F("–")));
  b += F("</div>");
  flush(b);

  // --- Geraeteinfo --------------------------------------------------------
  b += F("<h2>ESPWatcher</h2><table>");
  b += F("<tr><td>Firmware</td><td>");
  b += esc(s->localFwVersion);
  b += F("</td></tr><tr><td>Update</td><td>");
#if OTA_CONFIGURED
  b += esc(s->otaStatus);
  if (s->otaUpdateAvailable) {
    b += F(" (neu: ");
    b += esc(s->remoteFwVersion);
    b += F(")");
  }
  b += F(" · Auto-OTA ");
  b += otaAutoEnabled(*s) ? F("an") : F("aus");
  b += F(" (<a href='/ota?auto=");
  b += otaAutoEnabled(*s) ? F("0'>ausschalten") : F("1'>einschalten");
  b += F("</a>)");
  if (s->otaLastError.length()) {
    b += F("<br><span class='note'>");
    b += esc(s->otaLastError);
    b += F("</span>");
  }
#else
  b += F("OTA nicht konfiguriert");
#endif
  b += F("</td></tr><tr><td>IP / WLAN</td><td>");
  b += esc(s->deviceIp);
  b += F(" · ");
  b += String(WiFi.RSSI());
  b += F(" dBm</td></tr><tr><td>Uhrzeit</td><td>");
  b += esc(s->currentTime);
  b += F("</td></tr><tr><td>Netdata</td><td>");
  b += m.netdataReachable ? F("erreichbar") : F("nicht erreichbar");
  b += F("</td></tr><tr><td>Fehlende Messwerte</td><td>");
  b += m.unavailableMetrics.length() ? esc(m.unavailableMetrics) : String(F("keine"));
  b += F("</td></tr></table>");
  flush(b);

  endPage();
}

void sparkline(String& b, const char* title, const char* unit, const RingBuffer& buf, const char* color) {
  b += F("<h2>");
  b += title;
  b += F("</h2>");
  if (buf.count < 2) {
    b += F("<p>noch keine Daten</p>");
    return;
  }

  float lo = 1e9f;
  float hi = -1e9f;
  float last = 0;
  for (uint8_t i = 0; i < buf.count; ++i) {
    const uint8_t idx = (buf.head + HISTORY_SIZE - buf.count + i) % HISTORY_SIZE;
    const float v = buf.values[idx];
    if (v < lo) lo = v;
    if (v > hi) hi = v;
    last = v;
  }
  if (hi - lo < 1.0f) {
    hi = lo + 1.0f;
  }

  b += F("<svg viewBox='0 0 240 60' preserveAspectRatio='none'><polyline fill='none' stroke='");
  b += color;
  b += F("' stroke-width='2' vector-effect='non-scaling-stroke' points='");
  for (uint8_t i = 0; i < buf.count; ++i) {
    const uint8_t idx = (buf.head + HISTORY_SIZE - buf.count + i) % HISTORY_SIZE;
    const float x = i * 240.0f / (buf.count - 1);
    const float y = 56.0f - (buf.values[idx] - lo) / (hi - lo) * 52.0f;
    b += String(x, 1);
    b += ',';
    b += String(y, 1);
    b += ' ';
  }
  b += F("'/></svg><small>aktuell ");
  b += String(last, 1);
  b += unit;
  b += F(" · min ");
  b += String(lo, 1);
  b += F(" · max ");
  b += String(hi, 1);
  b += F("</small>");
}

void handleDashboard() { sendDashboard(); }

void handleCharts() {
  beginPage(true);
  String b;
  b += F("<p><small>Letzte ");
  b += String(HISTORY_SIZE * NETDATA_POLL_MS / 60000UL);
  b += F(" Minuten</small></p>");
  sparkline(b, "CPU-Auslastung", " %", s->cpuHistory, "#6cb2eb");
  flush(b);
  sparkline(b, "RAM-Belegung", " %", s->ramHistory, "#b58df0");
  flush(b);
  sparkline(b, "CPU-Temperatur", " °C", s->tempHistory, "#f39c5a");
  flush(b);
  endPage();
}

void handleLinks() {
  beginPage(false);
  String b;
  b += F("<ul>");
  b += F("<li><a href='http://" NETDATA_HOST ":" STR(NETDATA_PORT) "' target='_blank'>Netdata (piholepi)</a></li>");
  b += F("<li><a href='http://" PIHOLEPI_HOST "/admin' target='_blank'>Pi-hole piholepi</a></li>");
  b += F("<li><a href='http://" PIZERO_HOST "/admin' target='_blank'>Pi-hole pizero</a></li>");
  b += F("<li><a href='http://" PIHOLEPI_HOST ":8083' target='_blank'>dnsdist</a></li>");
  b += F("<li><a href='http://" PIHOLEPI_HOST ":20211' target='_blank'>NetAlertX</a></li>");
  b += F("<li><a href='http://" STATUS_HOST ":" STR(STATUS_PORT) STATUS_PATH "' target='_blank'>Status-JSON (Rohdaten)</a></li>");
  b += F("<li><a href='/api/state' target='_blank'>ESPWatcher-API</a></li>");
  b += F("</ul>");
  flush(b);
  endPage();
}

void handleApiState() {
  StaticJsonDocument<1536> doc;
  doc["health"] = healthToText(s->systemHealth);
  doc["reason"] = s->healthReason;
  doc["fw"] = s->localFwVersion;
  doc["active_dns"] = s->status.activeDns;
  doc["status_reachable"] = s->status.reachable;
  doc["netdata_reachable"] = s->latest.netdataReachable;
  if (!isnan(s->latest.cpuUsage)) doc["cpu"] = s->latest.cpuUsage;
  if (!isnan(s->latest.ramUsage)) doc["ram"] = s->latest.ramUsage;
  if (!isnan(s->latest.temperature)) doc["temp"] = s->latest.temperature;
  JsonArray svc = doc.createNestedArray("services");
  for (uint8_t i = 0; i < s->status.serviceCount; ++i) {
    JsonObject o = svc.createNestedObject();
    o["name"] = s->status.services[i].name;
    o["state"] = svcStateToText(s->status.services[i].state);
  }
  String out;
  serializeJson(doc, out);
  server.send(200, F("application/json"), out);
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
  server.on("/api/state", handleApiState);
  server.on("/ota", handleOtaToggle);
  server.begin();
}

void webLoop() {
  server.handleClient();
}

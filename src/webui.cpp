#include "webui.h"

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "config.h"
#include "health_eval.h"
#include "home_eval.h"
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
    "nav{display:flex;gap:2px;padding:8px 8px 0;background:#161c22;border-bottom:1px solid #2a323b;overflow-x:auto;white-space:nowrap}"
    "nav a{color:#9fb4c7;text-decoration:none;font-weight:600;padding:8px 12px;border-radius:8px 8px 0 0}"
    "nav a.act{background:#101418;color:#fff}"
    ".dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px;background:#5b6672}"
    ".dot.ok{background:#2ecc71}.dot.warn{background:#f1c40f}.dot.bad{background:#e74c3c}"
    ".cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:10px;margin-top:12px}"
    ".card{display:block;padding:12px 14px;border-radius:10px;background:#1b222a;border-left:8px solid #5b6672;color:inherit;text-decoration:none}"
    ".card b{display:block;font-size:16px;margin-bottom:2px}.card span{color:#c3ccd5;font-size:14px}"
    ".big{font-size:20px;margin-top:10px}.hero ul{margin:6px 0 0;padding-left:18px}"
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

struct Cat {
  uint8_t lvl = 0;  // 0 keine Daten, 1 ok, 2 Achtung, 3 Stoerung
  String text;
};

const char* lvlClass(uint8_t lvl) { return lvl == 1 ? "ok" : (lvl == 2 ? "warn" : (lvl == 3 ? "bad" : "")); }

struct Cats {
  Cat dns, svc, jobs, net, dev, hosts, sys, esp;
};

uint8_t worse(uint8_t a, uint8_t b) { return a > b ? a : b; }

Cat catDns(const StatusPoint& st) {
  Cat c;
  if (!st.reachable) {
    c.text = String(F("keine Daten vom Pi"));
    return c;
  }
  c.lvl = 1;
  c.text = String(F("aktiv: ")) + (st.activeDns == "unknown" ? String(F("unbekannt")) : st.activeDns);
  if (st.dnsMsVip >= 0) c.text += String(" · ") + st.dnsMsVip + " ms";
  const int16_t slow = max(st.dnsMsLocal, max(st.dnsMsVip, st.dnsMsPizero));
  if (st.dnsLocal == 0 || st.dnsVip == 0) {
    c.lvl = 3;
    c.text += String(F(" · antwortet nicht"));
  } else if (st.dnsPizero == 0) {
    c.lvl = 2;
    c.text += String(F(" · pizero ohne Antwort"));
  } else if (st.activeDns != "piholepi") {
    c.lvl = 2;
  } else if (slow > 800) {
    c.lvl = 2;
    c.text += String(F(" · langsam"));
  }
  return c;
}

Cat catSvc(const StatusPoint& st) {
  Cat c;
  if (!st.reachable || !st.serviceCount) {
    c.text = String(F("keine Daten vom Pi"));
    return c;
  }
  uint8_t running = 0;
  String bad;
  c.lvl = 1;
  for (uint8_t i = 0; i < st.serviceCount; ++i) {
    const ServiceEntry& e = st.services[i];
    if (e.state == SvcState::ACTIVE) {
      running++;
    } else if (e.state == SvcState::FAILED || e.state == SvcState::INACTIVE) {
      c.lvl = worse(c.lvl, e.critical ? 3 : 2);
      if (bad.length() < 40) bad += String(bad.length() ? ", " : "") + e.name;
    }
  }
  c.text = String(running) + "/" + st.serviceCount + " laufen";
  if (bad.length()) c.text += String(" · ") + bad;
  return c;
}

Cat catJobs(const StatusPoint& st) {
  Cat c;
  if (!st.reachable || !st.jobCount) {
    c.text = String(F("keine Aufträge"));
    return c;
  }
  c.lvl = 1;
  uint8_t good = 0;
  String bad;
  for (uint8_t i = 0; i < st.jobCount; ++i) {
    const JobEntry& j = st.jobs[i];
    if (j.state == JobState::OK || j.state == JobState::RUN) {
      good++;
    } else if (j.state == JobState::FAIL || j.state == JobState::LATE || j.state == JobState::OFF) {
      c.lvl = worse(c.lvl, j.state == JobState::FAIL ? 3 : 2);
      if (bad.length() < 40) bad += String(bad.length() ? ", " : "") + j.name;
    }
  }
  c.text = String(good) + "/" + st.jobCount + " in Ordnung";
  if (bad.length()) c.text += String(" · ") + bad;
  return c;
}

Cat catNet(const StatusPoint& st) {
  Cat c;
  const DiagInfo& d = st.diag;
  if (!d.present) {
    c.text = String(d.err[0] ? F("Diagnose nicht lesbar") : F("keine Diagnose"));
    return c;
  }
  c.lvl = 1;
  const bool wanKnown = d.wanDsl.length() || d.wanOnline.length();
  const bool wanOk = d.wanDsl == "online" && d.wanOnline == "online";
  if (wanKnown) c.text = String("Internet ") + (wanOk ? "online" : "gestört");
  uint8_t meshUp = 0;
  for (uint8_t i = 0; i < d.meshCount; ++i) meshUp += d.mesh[i].connected ? 1 : 0;
  if (d.meshCount) c.text += String(c.text.length() ? " · " : "") + "Mesh " + meshUp + "/" + d.meshCount;
  if (wanKnown && !wanOk) c.lvl = 3;
  if (c.lvl < 3 && (d.netLevel == 3 || meshUp < d.meshCount || d.ageS > 180)) c.lvl = 2;
  return c;
}

Cat catDev(const StatusPoint& st) {
  Cat c;
  const DiagInfo& d = st.diag;
  if (!d.present) {
    c.text = String(F("keine Diagnose"));
    return c;
  }
  c.lvl = 1;
  if (d.devTotal >= 0) c.text = String(d.devOnline) + "/" + d.devTotal + " erreichbar";
  if (d.offline.length()) {
    c.lvl = 2;
    c.text += String(" · offline: ") + d.offline;
  } else if (d.weak.length()) {
    c.text += String(" · Warnung: ") + d.weak;
  }
  return c;
}

Cat catHosts(const StatusPoint& st) {
  Cat c;
  const HomeInfo& h = st.home;
  if (!h.present) {
    c.text = String(F("keine Mirror-Daten"));
    return c;
  }
  c.lvl = 1;
  uint8_t bad = 0;
  for (uint8_t i = 0; i < h.hostCount; ++i) {
    const uint8_t l = hostLevel(h.hosts[i], nullptr);
    c.lvl = worse(c.lvl, l);
    if (l == 3) bad++;
  }
  c.text = String(h.hostCount - bad) + "/" + h.hostCount + " Hosts in Ordnung";
  if (h.nas.present) {
    const uint8_t l = nasLevel(h.nas, nullptr);
    c.lvl = worse(c.lvl, l);
    c.text += String(" · ") + h.nas.name + (l == 1 ? " in Ordnung" : (l == 2 ? " Achtung" : " gestört"));
  }
  return c;
}

Cat catSys(const StatusPoint& st, const MetricPoint& m) {
  Cat c;
  c.lvl = 1;
  uint8_t lvl = 1;
  auto bump = [&](float v, float warn, float bad) {
    if (isnan(v)) return;
    if (v >= bad) lvl = 3;
    else if (v >= warn && lvl < 2) lvl = 2;
  };
  bump(m.cpuUsage, 80, 95);
  bump(m.ramUsage, 85, 95);
  const float temp = isnan(m.temperature) ? st.cpuTemp : m.temperature;
  bump(temp, 70, 80);
  bump(st.diskUsedPct, 85, 95);
  if (st.throttled == 1 || st.diskMounted == 0 || m.criticalAlerts) lvl = 3;
  else if (m.warningAlerts && lvl < 2) lvl = 2;
  c.lvl = lvl;
  const float cpu = m.cpuUsage, ram = m.ramUsage;
  c.text = String(F("CPU ")) + fmt(cpu, 0, " %") + " · RAM " + fmt(ram, 0, " %") + " · " + fmt(temp, 0, " °C") + " · Platte " +
           fmt(st.diskUsedPct, 0, " %");
  return c;
}

Cat catEsp() {
  Cat c;
  c.lvl = (s->otaLastError.length() || WiFi.RSSI() < -80) ? 2 : 1;
  c.text = String("v") + s->localFwVersion + " · " + WiFi.RSSI() + " dBm";
  return c;
}

Cats allCats() {
  Cats c;
  c.dns = catDns(s->status);
  c.svc = catSvc(s->status);
  c.jobs = catJobs(s->status);
  c.net = catNet(s->status);
  c.dev = catDev(s->status);
  c.hosts = catHosts(s->status);
  c.sys = catSys(s->status, s->latest);
  c.esp = catEsp();
  return c;
}

void navLink(String& h, const char* href, const char* label, const char* active, uint8_t lvl) {
  h += F("<a href='");
  h += href;
  h += '\'';
  if (strcmp(active, href) == 0) h += F(" class='act'");
  h += F("><i class='dot ");
  h += lvlClass(lvl);
  h += F("'></i>");
  h += label;
  h += F("</a>");
}

void beginPage(const char* active, bool autoRefresh) {
  const Cats c = allCats();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, F("text/html; charset=utf-8"), "");
  String head;
  head += F("<!doctype html><html lang='de'><head><meta charset='utf-8'>");
  head += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  head += F("<title>ESPWatcher</title>");
  if (autoRefresh) head += F("<meta http-equiv='refresh' content='10'>");
  flush(head);
  server.sendContent_P(CSS);
  head += F("</head><body><nav>");
  const uint8_t overall = static_cast<uint8_t>(s->systemHealth) + 1;
  navLink(head, "/", "Übersicht", active, overall);
  navLink(head, "/dns", "DNS", active, c.dns.lvl);
  navLink(head, "/dienste", "Dienste", active, c.svc.lvl);
  navLink(head, "/sicherungen", "Sicherungen", active, c.jobs.lvl);
  navLink(head, "/netz", "Netz", active, c.net.lvl);
  navLink(head, "/geraete", "Geräte", active, c.dev.lvl);
  navLink(head, "/hosts", "Hosts", active, c.hosts.lvl);
  navLink(head, "/system", "System", active, c.sys.lvl);
  navLink(head, "/charts", "Verlauf", active, 0);
  navLink(head, "/esp", "ESP", active, c.esp.lvl);
  head += F("</nav><main>");
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

void card(String& b, const char* href, const char* title, const Cat& c) {
  b += F("<a class='card ");
  b += lvlClass(c.lvl);
  b += F("' href='");
  b += href;
  b += F("'><b>");
  b += title;
  b += F("</b><span>");
  b += esc(c.text);
  b += F("</span></a>");
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

const char* jobClass(JobState st) {
  switch (st) {
    case JobState::OK: return "ok";
    case JobState::RUN: return "ok";
    case JobState::FAIL: return "bad";
    case JobState::LATE: return "warn";
    case JobState::OFF: return "warn";
    default: return "";
  }
}

const char* levelClass(uint8_t level) { return level == 1 ? "ok" : (level == 2 ? "warn" : (level == 3 ? "bad" : "")); }

String hoursText(float h) {
  if (isnan(h)) return String(F("–"));
  if (h < 1.0f) return String(F("< 1 h"));
  if (h < 48.0f) return String(h, 0) + " h";
  return String(h / 24.0f, 0) + " Tage";
}

String dnsText(int8_t ok, int16_t ms) {
  String t = checkText(ok);
  if (ok == 1 && ms >= 0) t += String(" (") + ms + " ms)";
  return t;
}

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

void noData(String& b, const StatusPoint& st) {
  if (!st.reachable) {
    b += F("<div class='banner note'>Status-JSON nicht erreichbar");
    if (st.timestampMs) {
      b += F(" – letzte Daten vor ");
      b += ageText(st.timestampMs);
    }
    b += F("</div>");
  }
}

// ---- Uebersicht ----------------------------------------------------------------
void sendOverview() {
  const StatusPoint& st = s->status;
  const Cats c = allCats();
  beginPage("/", true);
  String b;

  b += F("<div class='hero ");
  b += healthToText(s->systemHealth);
  b += F("'><b>");
  b += healthToGerman(s->systemHealth);
  b += F("</b>");
  if (s->systemHealth != HealthState::GREEN && s->healthReason.length()) {
    b += F("<ul>");
    int start = 0;
    while (start < static_cast<int>(s->healthReason.length())) {
      int end = s->healthReason.indexOf("; ", start);
      if (end < 0) end = s->healthReason.length();
      b += F("<li>");
      b += esc(s->healthReason.substring(start, end));
      b += F("</li>");
      start = end + 2;
    }
    b += F("</ul>");
  } else {
    b += F("<div>Keine Probleme erkannt.</div>");
  }
  b += F("</div>");
  noData(b, st);
  flush(b);

  b += F("<div class='cards'>");
  card(b, "/dns", "DNS", c.dns);
  card(b, "/dienste", "Dienste", c.svc);
  card(b, "/sicherungen", "Sicherungen", c.jobs);
  card(b, "/netz", "Netz", c.net);
  card(b, "/geraete", "Geräte", c.dev);
  card(b, "/hosts", "Hosts & DiskStation", c.hosts);
  card(b, "/system", "System piholepi", c.sys);
  card(b, "/esp", "ESPWatcher", c.esp);
  b += F("</div>");
  flush(b);
  endPage();
}

// ---- DNS -----------------------------------------------------------------------
void sendDns() {
  const StatusPoint& st = s->status;
  beginPage("/dns", true);
  String b;
  noData(b, st);
  if (st.activeDns != "unknown") {
    b += F("<div class='banner big'>Aktiver DNS: <b>");
    b += esc(st.activeDns);
    b += F("</b> (" DNS_VIP ")</div>");
  }
  b += F("<h2>Erreichbarkeit und Antwortzeit</h2><div class='tiles'>");
  tile(b, checkClass(st.dnsLocal), "piholepi", dnsText(st.dnsLocal, st.dnsMsLocal));
  tile(b, checkClass(st.dnsVip), "VIP " DNS_VIP, dnsText(st.dnsVip, st.dnsMsVip));
  tile(b, checkClass(st.dnsPizero), "pizero", dnsText(st.dnsPizero, st.dnsMsPizero));
  b += F("</div>");
  if (st.diag.present && st.diag.failovers24h >= 0) {
    b += F("<h2>Umschaltungen</h2><div class='tiles'>");
    tile(b, st.diag.failovers24h > 0 ? "warn" : "ok", "letzte 24 Stunden", String(st.diag.failovers24h));
    tile(b, "", "letzte 7 Tage", String(st.diag.failovers7d));
    b += F("</div>");
  }
  flush(b);
  endPage();
}

// ---- Dienste -------------------------------------------------------------------
void sendServices() {
  const StatusPoint& st = s->status;
  beginPage("/dienste", true);
  String b;
  noData(b, st);
  if (st.serviceCount) {
    b += F("<h2>Kritisch (Ausfall = Störung)</h2><div class='tiles'>");
    for (uint8_t i = 0; i < st.serviceCount; ++i) {
      if (st.services[i].critical) tile(b, svcClass(st.services[i]), esc(st.services[i].name).c_str(), svcStateToText(st.services[i].state));
    }
    b += F("</div><h2>Weitere Dienste (Ausfall = Achtung)</h2><div class='tiles'>");
    for (uint8_t i = 0; i < st.serviceCount; ++i) {
      if (!st.services[i].critical) tile(b, svcClass(st.services[i]), esc(st.services[i].name).c_str(), svcStateToText(st.services[i].state));
    }
    b += F("</div>");
  }
  flush(b);
  endPage();
}

// ---- Sicherungen ---------------------------------------------------------------
void sendJobs() {
  const StatusPoint& st = s->status;
  beginPage("/sicherungen", true);
  String b;
  noData(b, st);
  if (st.jobCount) {
    b += F("<h2>Sicherungen &amp; Aufträge</h2><div class='tiles'>");
    for (uint8_t i = 0; i < st.jobCount; ++i) {
      const JobEntry& j = st.jobs[i];
      String v = jobStateToText(j.state);
      if ((j.state == JobState::OK || j.state == JobState::LATE || j.state == JobState::FAIL) && !isnan(j.ageH)) {
        v += String(F(" · vor ")) + hoursText(j.ageH);
      }
      tile(b, jobClass(j.state), esc(j.name).c_str(), v);
    }
    b += F("</div><p><small>„überfällig“ heißt: älter als zwei Timer-Perioden. „Fehler“: der letzte Lauf ist fehlgeschlagen.</small></p>");
  } else {
    b += F("<p>Keine Aufträge gemeldet.</p>");
  }
  if (!isnan(st.backupAgeH)) {
    b += F("<h2>Backup-Marker</h2><div class='tiles'>");
    tile(b, thresholdClass(st.backupAgeH, 30, 36), "Letztes Backup", fmt(st.backupAgeH, 0, " h her"));
    b += F("</div>");
  }
  flush(b);
  endPage();
}

// ---- Netz und Geraete ----------------------------------------------------------
void sendNet() {
  const StatusPoint& st = s->status;
  const DiagInfo& d = st.diag;
  beginPage("/netz", true);
  String b;
  noData(b, st);
  if (d.present) {
    b += F("<h2>Netzwerk</h2><div class='tiles'>");
    tile(b, levelClass(d.netLevel), "Zustand", d.netText.length() ? esc(d.netText) : String(F("ok")));
    if (d.wanDsl.length() || d.wanOnline.length()) {
      const bool wanOk = d.wanDsl == "online" && d.wanOnline == "online";
      tile(b, wanOk ? "ok" : "bad", "Internet (DSL)", wanOk ? String(F("online")) : esc(d.wanDsl + " / " + d.wanOnline));
    }
    if (d.wanDown || d.wanUp) {
      // Der Router meldet die Anschlussgeschwindigkeit in kbit/s (keine aktuelle Last)
      tile(b, "", "Anschluss laut Router (Down / Up)",
           String(d.wanDown / 1000UL) + " / " + String(d.wanUp / 1000UL) + " Mbit/s");
    }
    b += F("</div>");
    if (d.meshCount) {
      b += F("<h2>Mesh</h2><div class='tiles'>");
      for (uint8_t i = 0; i < d.meshCount; ++i) {
        tile(b, d.mesh[i].connected ? "ok" : "bad", esc(d.mesh[i].name).c_str(),
             d.mesh[i].connected ? String(F("verbunden")) : String(F("getrennt")));
      }
      b += F("</div>");
    }
    if (d.ageS > 180) {
      b += F("<div class='banner note'>Diagnose-Daten sind veraltet</div>");
    }
  } else if (d.err[0]) {
    b += F("<div class='banner note'>Netz-Diagnose auf dem Pi nicht lesbar (");
    b += esc(String(d.err));
    b += F(") – Rechte auf /run/mirror-diag/diag.json prüfen</div>");
  } else {
    b += F("<p>Keine Netz-Diagnose vom Pi (mirror-diag nicht aktiv).</p>");
  }
  flush(b);
  endPage();
}

// ---- Geraete -------------------------------------------------------------------
uint8_t devGroup(const char* name) {
  if (strncmp(name, "infra-", 6) == 0) return 1;
  if (strncmp(name, "voice-", 6) == 0) return 2;
  if (strncmp(name, "client-", 7) == 0) return 3;
  return 0;
}

const char* devDisplay(const char* name) {
  const uint8_t g = devGroup(name);
  return g == 1 || g == 2 ? name + 6 : (g == 3 ? name + 7 : name);
}

void sendDevices() {
  const StatusPoint& st = s->status;
  const DiagInfo& d = st.diag;
  beginPage("/geraete", true);
  String b;
  noData(b, st);
  if (!d.present) {
    b += F("<p>Keine Geräte-Diagnose vom Pi (mirror-diag nicht aktiv oder nicht lesbar).</p>");
    flush(b);
    endPage();
    return;
  }
  b += F("<h2>Überblick</h2><div class='tiles'>");
  tile(b, levelClass(d.devLevel == 3 ? 2 : (d.devLevel ? 1 : 0)), "Erreichbar",
       d.devTotal >= 0 ? String(d.devOnline) + "/" + d.devTotal : String(F("–")));
  if (d.offline.length()) tile(b, "bad", "Offline", esc(d.offline));
  if (d.weak.length()) tile(b, "warn", "Warnung (z. B. schwaches WLAN)", esc(d.weak));
  b += F("</div>");
  flush(b);

  static const char* const groups[] = {"Server &amp; Pis", "Infrastruktur", "Sprachassistenten", "Clients"};
  for (uint8_t g = 0; g < 4; ++g) {
    bool any = false;
    for (uint8_t i = 0; i < d.devCount; ++i) {
      const DevEntry& e = d.devs[i];
      if (devGroup(e.name) != g) continue;
      if (!any) {
        b += F("<h2>");
        b += groups[g];
        b += F("</h2><div class='tiles'>");
        any = true;
      }
      String v = e.online ? String(F("online")) : String(F("offline"));
      if (e.online && e.ms >= 0) v += String(" · ") + e.ms + " ms";
      if (e.rssi != 0) v += String(" · ") + e.rssi + " dBm";
      tile(b, levelClass(e.state), esc(devDisplay(e.name)).c_str(), v);
    }
    if (any) {
      b += F("</div>");
      flush(b);
    }
  }
  endPage();
}

// ---- System --------------------------------------------------------------------
void sendSystem() {
  const StatusPoint& st = s->status;
  const MetricPoint& m = s->latest;
  beginPage("/system", true);
  String b;
  noData(b, st);
  b += F("<h2>Auslastung</h2><div class='tiles'>");
  tile(b, thresholdClass(m.cpuUsage, 80, 95), "CPU", fmt(m.cpuUsage, 0, " %"));
  tile(b, thresholdClass(m.ramUsage, 85, 95), "RAM", fmt(m.ramUsage, 0, " %"));
  tile(b, thresholdClass(isnan(m.temperature) ? st.cpuTemp : m.temperature, 70, 80), "Temperatur",
       fmt(isnan(m.temperature) ? st.cpuTemp : m.temperature, 1, " °C"));
  tile(b, "", "Load (1 min)", fmt(isnan(m.load1) ? st.load1 : m.load1, 2, ""));
  b += F("</div><h2>Speicher und Hardware</h2><div class='tiles'>");
  if (st.diskMounted == 0) {
    tile(b, "bad", "Datenplatte", String(F("nicht eingehängt")));
  } else {
    tile(b, thresholdClass(st.diskUsedPct, 85, 95), "Datenplatte", fmt(st.diskUsedPct, 0, " % voll"));
    tile(b, "", "Frei", fmt(st.diskFreeGb, 1, " GB"));
  }
  if (st.throttled >= 0) {
    tile(b, st.throttled ? "bad" : "ok", "Pi-Drosselung", st.throttled ? String(F("aktiv")) : String(F("keine")));
  }
  tile(b, m.criticalAlerts ? "bad" : (m.warningAlerts ? "warn" : (m.netdataReachable ? "ok" : "")), "Netdata-Alarme",
       m.netdataReachable ? String(m.criticalAlerts) + " krit. / " + String(m.warningAlerts) + " warn." : String(F("–")));
  b += F("</div>");
  flush(b);
  endPage();
}

// ---- ESP -----------------------------------------------------------------------
void sendEsp() {
  const MetricPoint& m = s->latest;
  beginPage("/esp", false);
  String b;
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
  b += F("</td></tr><tr><td>Freier Speicher</td><td>");
  b += String(ESP.getFreeHeap() / 1024UL);
  b += F(" KB</td></tr><tr><td>Laufzeit</td><td>");
  b += String(millis() / 3600000UL);
  b += F(" h</td></tr><tr><td>Netdata</td><td>");
  b += m.netdataReachable ? F("erreichbar") : F("nicht erreichbar");
  b += F("</td></tr><tr><td>Fehlende Netdata-Werte</td><td>");
  b += m.unavailableMetrics.length() ? esc(m.unavailableMetrics) : String(F("keine"));
  b += F("</td></tr></table>");
  flush(b);

  b += F("<h2>Links</h2><ul>");
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

void sparkline(String& b, const char* title, const char* unit, const RingBuffer& buf, const char* color) {
  b += F("<h2>");
  b += title;
  b += F("</h2>");
  if (buf.count < 2) {
    b += F("<p>noch keine Daten (mindestens zwei Messpunkte nötig, ca. 1 Minute)</p>");
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

String uptimeText(int32_t sec) {
  if (sec < 0) return String(F("–"));
  if (sec < 3600) return String(sec / 60) + " min";
  if (sec < 172800) return String(sec / 3600) + " h";
  return String(sec / 86400) + " Tage";
}

void row(String& b, const char* label, const String& value, const char* cls = "") {
  b += F("<tr><td>");
  b += label;
  b += F("</td><td class='");
  b += cls;
  b += F("'>");
  b += value;
  b += F("</td></tr>");
}

void hostBlock(String& b, const HostEntry& h) {
  String note;
  const uint8_t lvl = hostLevel(h, &note);
  b += F("<h2><i class='dot ");
  b += lvlClass(lvl);
  b += F("'></i>");
  b += esc(h.name);
  b += F("</h2><table>");
  if (!h.ok) {
    row(b, "Zustand", esc(note), "note");
    b += F("</table>");
    return;
  }
  row(b, "CPU", fmt(h.cpu < 0 ? NAN : h.cpu, 0, " %"));
  row(b, "RAM", fmt(h.mem < 0 ? NAN : h.mem, 0, " %"));
  String disk = fmt(h.disk < 0 ? NAN : h.disk, 0, " %");
  if (h.freeGb >= 0) disk += String(" · ") + String(h.freeGb, 0) + " GB frei";
  row(b, "Speicher", disk);
  row(b, "Temperatur", fmt(h.temp < 0 ? NAN : h.temp, 0, " °C"));
  row(b, "läuft seit", uptimeText(h.uptimeS));
  String upd = h.updates < 0 ? String(F("–")) : (h.updates == 0 ? String(F("aktuell")) : String(h.updates) + " offen");
  if (h.security > 0) upd += String(" · ") + h.security + " Sicherheit";
  row(b, "Updates", upd, h.security > 0 ? "note" : "");
  row(b, "letztes Update", h.lastUpdateH < 0 ? String(F("–")) : hoursText(h.lastUpdateH));
  row(b, "Zustand", note.length() ? esc(note) : String(F("in Ordnung")), lvl > 1 ? "note" : "");
  b += F("</table>");
}

void nasBlock(String& b, const NasInfo& n) {
  String note;
  const uint8_t lvl = nasLevel(n, &note);
  b += F("<h2><i class='dot ");
  b += lvlClass(lvl);
  b += F("'></i>");
  b += esc(n.name);
  b += F(" (DiskStation)</h2><table>");
  if (!n.online) {
    row(b, "Zustand", esc(note), "note");
    b += F("</table>");
    return;
  }
  row(b, "Zustand", note.length() ? esc(note) : String(F("in Ordnung")), lvl > 1 ? "note" : "");
  for (uint8_t i = 0; i < n.raidCount; ++i) {
    row(b, "RAID", raidText(n.raid[i]), n.raid[i] == 1 ? "" : "note");
  }
  for (uint8_t i = 0; i < n.diskCount; ++i) {
    const NasDisk& d = n.disks[i];
    String v = d.temp >= 0 ? String(d.temp) + " °C" : String(F("–"));
    if (d.status >= 0 && d.status != 1) v += F(" · gestört");
    row(b, esc(d.name).c_str(), v, d.temp >= 50 ? "note" : "");
  }
  row(b, "System", n.sysTemp >= 0 ? String(n.sysTemp) + " °C" : String(F("–")), n.sysTemp >= 55 ? "note" : "");
  row(b, "Netzteil", n.power < 0 ? String(F("unbekannt")) : (n.power == 1 ? String(F("in Ordnung")) : String(F("gestört"))),
      (n.power >= 0 && n.power != 1) ? "note" : "");
  if (n.volUsedPct >= 0) {
    row(b, "Volume", String(n.volUsedPct, 0) + " % von " + String(n.volSizeGb / 1000.0f, 1) + " TB");
  }
  b += F("</table>");
}

void sendHosts() {
  beginPage("/hosts", true);
  String b;
  const HomeInfo& h = s->status.home;
  noData(b, s->status);
  if (!h.present) {
    b += F("<div class='banner note'>Keine Mirror-Daten: ");
    b += h.err[0] ? esc(h.err) : String(F("homedash.json fehlt auf dem Pi"));
    b += F("</div>");
  } else {
    if (h.ageS > 300) b += F("<div class='banner note'>Mirror-Daten veraltet</div>");
    for (uint8_t i = 0; i < h.hostCount; ++i) {
      hostBlock(b, h.hosts[i]);
      flush(b);
    }
    if (h.nas.present) nasBlock(b, h.nas);
  }
  flush(b);
  endPage();
}

void handleCharts() {
  beginPage("/charts", true);
  String b;
  b += F("<p><small>Letzte ");
  b += String(HISTORY_SIZE * HISTORY_STEP_MS / 60000UL);
  b += F(" Minuten (ein Punkt alle ");
  b += String(HISTORY_STEP_MS / 1000UL);
  b += F(" s, nach einem Neustart leer)</small></p>");
  sparkline(b, "CPU-Auslastung", " %", s->cpuHistory, "#6cb2eb");
  flush(b);
  sparkline(b, "RAM-Belegung", " %", s->ramHistory, "#b58df0");
  flush(b);
  sparkline(b, "CPU-Temperatur", " °C", s->tempHistory, "#f39c5a");
  flush(b);
  sparkline(b, "DNS-Antwortzeit (VIP)", " ms", s->dnsHistory, "#4cc9a0");
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
  server.on("/", sendOverview);
  server.on("/dns", sendDns);
  server.on("/dienste", sendServices);
  server.on("/sicherungen", sendJobs);
  server.on("/netz", sendNet);
  server.on("/geraete", sendDevices);
  server.on("/hosts", sendHosts);
  server.on("/system", sendSystem);
  server.on("/esp", sendEsp);
  server.on("/links", sendEsp);
  server.on("/charts", handleCharts);
  server.on("/api/state", handleApiState);
  server.on("/ota", handleOtaToggle);
  server.begin();
}

void webLoop() {
  server.handleClient();
}

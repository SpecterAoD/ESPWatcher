#include "status_client.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>

#include "config.h"

namespace {
SvcState parseSvcState(const char* text) {
  if (strcmp(text, "active") == 0) return SvcState::ACTIVE;
  if (strcmp(text, "inactive") == 0) return SvcState::INACTIVE;
  if (strcmp(text, "failed") == 0) return SvcState::FAILED;
  if (strcmp(text, "missing") == 0) return SvcState::MISSING;
  return SvcState::UNKNOWN;
}

JobState parseJobState(const char* text) {
  if (strcmp(text, "ok") == 0) return JobState::OK;
  if (strcmp(text, "late") == 0) return JobState::LATE;
  if (strcmp(text, "fail") == 0) return JobState::FAIL;
  if (strcmp(text, "run") == 0) return JobState::RUN;
  if (strcmp(text, "off") == 0) return JobState::OFF;
  if (strcmp(text, "missing") == 0) return JobState::MISSING;
  return JobState::UNKNOWN;
}

uint8_t parseLevel(const char* text) {
  if (strcmp(text, "gruen") == 0) return 1;
  if (strcmp(text, "gelb") == 0) return 2;
  if (strcmp(text, "rot") == 0) return 3;
  return 0;
}

int16_t optInt(JsonVariantConst v) {
  if (v.isNull()) return -1;
  return static_cast<int16_t>(v.as<int>());
}

int8_t triState(JsonVariantConst v) {
  if (v.isNull()) return -1;
  return v.as<int>() ? 1 : 0;
}

float optFloat(JsonVariantConst v) {
  if (v.isNull()) return NAN;
  return v.as<float>();
}
}  // namespace

const char* jobStateToText(JobState state) {
  switch (state) {
    case JobState::OK: return "ok";
    case JobState::LATE: return "überfällig";
    case JobState::FAIL: return "Fehler";
    case JobState::RUN: return "läuft";
    case JobState::OFF: return "Timer aus";
    case JobState::MISSING: return "fehlt";
    default: return "?";
  }
}

const char* svcStateToText(SvcState state) {
  switch (state) {
    case SvcState::ACTIVE:
      return "aktiv";
    case SvcState::INACTIVE:
      return "gestoppt";
    case SvcState::FAILED:
      return "Fehler";
    case SvcState::MISSING:
      return "fehlt";
    default:
      return "?";
  }
}

bool statusPoll(StatusPoint& out) {
  out = StatusPoint{};

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String url = "http://";
  url += STATUS_HOST;
  url += ":";
  url += String(STATUS_PORT);
  url += STATUS_PATH;

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) {
    return false;
  }
  // HTTP/1.0 verhindert "chunked"-Antworten, die getStream() nicht dekodiert.
  http.useHTTP10(true);
  http.setTimeout(STATUS_TIMEOUT_MS);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    return false;
  }

  out.reachable = true;
  out.timestampMs = millis();

  // Nur lesender Zugriff ueber eine konstante Sicht auf das Dokument.
  JsonVariantConst root = doc.as<JsonVariantConst>();

  const float ageS = root["age_s"] | 0.0f;
  out.stale = ageS > STATUS_STALE_S;

  out.activeDns = String(root["active"] | "unknown");
  out.dnsLocal = triState(root["dns"]["local"]);
  out.dnsVip = triState(root["dns"]["vip"]);
  out.dnsPizero = triState(root["dns"]["pizero"]);

  out.dnsMsLocal = optInt(root["dns_ms"]["local"]);
  out.dnsMsVip = optInt(root["dns_ms"]["vip"]);
  out.dnsMsPizero = optInt(root["dns_ms"]["pizero"]);

  JsonArrayConst svc = root["svc"].as<JsonArrayConst>();
  for (JsonObjectConst item : svc) {
    if (out.serviceCount >= MAX_SERVICES) break;
    ServiceEntry& e = out.services[out.serviceCount++];
    strlcpy(e.name, item["n"] | "?", sizeof(e.name));
    e.state = parseSvcState(item["s"] | "");
    e.critical = (item["c"] | 0) != 0;
  }

  out.cpuTemp = optFloat(root["temp"]);
  out.load1 = optFloat(root["load1"]);
  out.memUsedPct = optFloat(root["mem"]);
  out.throttled = triState(root["thr"]);

  out.diskMounted = triState(root["disk"]["mounted"]);
  out.diskUsedPct = optFloat(root["disk"]["used"]);
  out.diskFreeGb = optFloat(root["disk"]["free_gb"]);
  out.backupAgeH = optFloat(root["backup_h"]);

  JsonArrayConst jobs = root["jobs"].as<JsonArrayConst>();
  for (JsonObjectConst item : jobs) {
    if (out.jobCount >= MAX_JOBS) break;
    JobEntry& j = out.jobs[out.jobCount++];
    strlcpy(j.name, item["n"] | "?", sizeof(j.name));
    j.state = parseJobState(item["s"] | "");
    j.ageH = optFloat(item["a"]);
  }

  JsonVariantConst diag = root["diag"];
  if (!diag.isNull()) {
    DiagInfo& d = out.diag;
    strlcpy(d.err, diag["err"] | "", sizeof(d.err));
    d.present = d.err[0] == 0;
    d.ageS = static_cast<uint16_t>(diag["age_s"] | 0);
    d.netLevel = parseLevel(diag["net"]["s"] | "");
    d.devLevel = parseLevel(diag["dev"]["s"] | "");
    d.netText = String(diag["net"]["t"] | "");
    d.devText = String(diag["dev"]["t"] | "");
    for (JsonVariantConst name : diag["off"].as<JsonArrayConst>()) {
      if (d.offline.length()) d.offline += ", ";
      d.offline += String(name | "?");
    }
    for (JsonVariantConst name : diag["warn"].as<JsonArrayConst>()) {
      if (d.weak.length()) d.weak += ", ";
      d.weak += String(name | "?");
    }
    for (JsonArrayConst row : diag["devs"].as<JsonArrayConst>()) {
      if (d.devCount >= MAX_DEVS) break;
      DevEntry& e = d.devs[d.devCount++];
      strlcpy(e.name, row[0] | "?", sizeof(e.name));
      e.state = static_cast<uint8_t>(row[1] | 0);
      e.online = (row[2] | 0) != 0;
      e.ms = static_cast<int16_t>(row[3] | -1);
      e.rssi = static_cast<int16_t>(row[4] | 0);
    }
    d.devOnline = optInt(diag["n_dev"][0]);
    d.devTotal = optInt(diag["n_dev"][1]);
    d.wanDsl = String(diag["wan"]["dsl"] | "");
    d.wanOnline = String(diag["wan"]["on"] | "");
    d.wanDown = diag["wan"]["down"] | 0UL;
    d.wanUp = diag["wan"]["up"] | 0UL;
    for (JsonObjectConst node : diag["mesh"].as<JsonArrayConst>()) {
      if (d.meshCount >= MAX_MESH) break;
      MeshEntry& m = d.mesh[d.meshCount++];
      strlcpy(m.name, node["n"] | "?", sizeof(m.name));
      m.connected = (node["c"] | 0) != 0;
    }
    d.failovers24h = optInt(diag["fo"][0]);
    d.failovers7d = optInt(diag["fo"][1]);
  }

  return true;
}

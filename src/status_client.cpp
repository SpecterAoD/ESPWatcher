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

int8_t triState(JsonVariantConst v) {
  if (v.isNull()) return -1;
  return v.as<int>() ? 1 : 0;
}

float optFloat(JsonVariantConst v) {
  if (v.isNull()) return NAN;
  return v.as<float>();
}
}  // namespace

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

  DynamicJsonDocument doc(3072);
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

  return true;
}

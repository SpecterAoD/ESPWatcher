#include "ota_mgr.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266WiFi.h>

#include "config.h"
#include "version.h"

namespace {
int compareVersion(const char* a, const char* b) {
  int ai = 0;
  int bi = 0;
  while (a[ai] != '\0' || b[bi] != '\0') {
    int na = 0;
    int nb = 0;
    while (a[ai] >= '0' && a[ai] <= '9') na = na * 10 + (a[ai++] - '0');
    while (b[bi] >= '0' && b[bi] <= '9') nb = nb * 10 + (b[bi++] - '0');
    if (na != nb) return na < nb ? -1 : 1;
    if (a[ai] == '.') ai++;
    if (b[bi] == '.') bi++;
  }
  return 0;
}

String baseUrl() {
  String out = "http://";
  out += NETDATA_HOST;
  out += ":";
  out += String(NETDATA_PORT);
  return out;
}
}  // namespace

void otaSetup(AppState& state) {
  state.otaAutoEnabled = OTA_AUTO_UPDATE_DEFAULT;
  state.otaStatus = "ready";
}

void otaSetAuto(AppState& state, bool enabled) {
  state.otaAutoEnabled = enabled;
}

bool otaAutoEnabled(const AppState& state) {
  return state.otaAutoEnabled;
}

void otaLoop(AppState& state) {
  if (!state.wifiConnected || millis() - state.lastOtaCheckMs < OTA_CHECK_MS) {
    return;
  }
  state.lastOtaCheckMs = millis();

  WiFiClient client;
  HTTPClient http;
  const String manifestUrl = baseUrl() + OTA_MANIFEST_PATH;
  if (!http.begin(client, manifestUrl)) {
    state.otaStatus = "manifest begin failed";
    return;
  }

  http.setTimeout(NETDATA_TIMEOUT_MS);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    state.otaStatus = "manifest unavailable";
    http.end();
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    state.otaStatus = "manifest parse failed";
    return;
  }

  const char* remoteVersion = doc["version"] | "";
  const char* firmwarePath = doc["url"] | "";
  const char* md5 = doc["md5"] | "";

  if (compareVersion(remoteVersion, FW_VERSION) <= 0) {
    state.otaStatus = "up-to-date";
    return;
  }

  state.otaStatus = String("new version ") + remoteVersion;
  if (!state.otaAutoEnabled) {
    return;
  }

  ESPhttpUpdate.rebootOnUpdate(true);
  if (strlen(md5) == 32) {
    ESPhttpUpdate.setMD5(md5);
  }

  t_httpUpdate_return result = ESPhttpUpdate.update(client, baseUrl() + firmwarePath, FW_VERSION);
  switch (result) {
    case HTTP_UPDATE_FAILED:
      state.otaStatus = String("update failed: ") + ESPhttpUpdate.getLastErrorString();
      break;
    case HTTP_UPDATE_NO_UPDATES:
      state.otaStatus = "no updates";
      break;
    case HTTP_UPDATE_OK:
      state.otaStatus = "update applied";
      break;
  }
}

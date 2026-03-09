#include "ota_mgr.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>

#include "config.h"
#include "logger.h"
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

String otaBaseUrl() {
  String out = "http://";
  out += OTA_HOST;
  out += ":";
  out += String(OTA_PORT);
  return out;
}

String absoluteFirmwareUrl(const String& path) {
  if (path.startsWith("http://") || path.startsWith("https://")) {
    return path;
  }
  if (path.length() == 0) {
    return otaBaseUrl() + "/firmware.bin";
  }
  if (path[0] == '/') {
    return otaBaseUrl() + path;
  }
  return otaBaseUrl() + "/" + path;
}

void setOtaError(AppState& state, const String& message) {
  state.otaLastError = message;
  state.otaStatus = message;
  logMessage("OTA", message, LogLevel::ERR);
}
}  // namespace

void otaSetup(AppState& state) {
  state.otaAutoEnabled = OTA_AUTO_UPDATE_DEFAULT;
  state.localFwVersion = FW_VERSION;
  state.remoteFwVersion = "unknown";
  state.otaLastError = "";
  state.otaStatus = "idle";
  state.otaUpdateAvailable = false;
  state.otaFirmwareUrl = otaBaseUrl() + "/firmware.bin";
  logMessage("OTA", String("OTA configured with manifest ") + otaBaseUrl() + OTA_MANIFEST_PATH);
}

void otaSetAuto(AppState& state, bool enabled) {
  state.otaAutoEnabled = enabled;
  logMessage("OTA", String("Automatic OTA ") + (enabled ? "enabled" : "disabled"));
}

bool otaAutoEnabled(const AppState& state) {
  return state.otaAutoEnabled;
}

void otaLoop(AppState& state) {
  if (!state.wifiConnected || millis() - state.lastOtaCheckMs < OTA_CHECK_MS) {
    return;
  }
  state.lastOtaCheckMs = millis();
  state.otaStatus = "checking";
  state.otaLastError = "";
  logMessage("OTA", "Checking manifest");

  WiFiClient client;
  HTTPClient http;
  const String manifestUrl = otaBaseUrl() + OTA_MANIFEST_PATH;

  if (!http.begin(client, manifestUrl)) {
    setOtaError(state, "Manifest begin failed");
    return;
  }

  http.setTimeout(NETDATA_TIMEOUT_MS);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    String msg = String("Manifest fetch failed HTTP ") + code;
    http.end();
    setOtaError(state, msg);
    return;
  }

  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    setOtaError(state, String("Manifest parse failed: ") + err.c_str());
    return;
  }

  const char* remoteVersion = doc["version"] | "";
  const char* firmwarePath = doc["url"] | "/firmware.bin";
  const char* md5 = doc["md5"] | "";

  if (strlen(remoteVersion) == 0) {
    setOtaError(state, "Manifest missing version");
    return;
  }

  state.remoteFwVersion = remoteVersion;
  state.otaFirmwareUrl = absoluteFirmwareUrl(firmwarePath);

  logMessage("OTA", String("Remote version ") + state.remoteFwVersion);
  logMessage("OTA", String("Firmware URL ") + state.otaFirmwareUrl);

  int cmp = compareVersion(remoteVersion, FW_VERSION);
  if (cmp <= 0) {
    state.otaUpdateAvailable = false;
    state.otaStatus = "Up to date";
    logMessage("OTA", String("No update needed. Local ") + FW_VERSION + ", remote " + remoteVersion);
    return;
  }

  state.otaUpdateAvailable = true;
  state.otaStatus = "New firmware available";
  logMessage("OTA", String("Update available. Local ") + FW_VERSION + ", remote " + remoteVersion, LogLevel::WARN);

  if (!state.otaAutoEnabled) {
    logMessage("OTA", "Auto update disabled; skipping download", LogLevel::WARN);
    return;
  }

  state.otaStatus = "downloading";
  logMessage("OTA", "Starting OTA download and update");
  ESPhttpUpdate.rebootOnUpdate(true);
  if (strlen(md5) == 32) {
    ESPhttpUpdate.setMD5sum(md5);
    logMessage("OTA", "MD5 from manifest accepted");
  } else if (strlen(md5) > 0) {
    logMessage("WARN", "Manifest MD5 invalid length; ignored", LogLevel::WARN);
  }

  t_httpUpdate_return result = ESPhttpUpdate.update(client, state.otaFirmwareUrl, FW_VERSION);
  switch (result) {
    case HTTP_UPDATE_FAILED:
      setOtaError(state, String("Firmware update failed: ") + ESPhttpUpdate.getLastErrorString());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      state.otaUpdateAvailable = false;
      state.otaStatus = "No update from server";
      logMessage("OTA", "HTTP updater reported no updates");
      break;
    case HTTP_UPDATE_OK:
      state.otaStatus = "OTA update successful, rebooting";
      logMessage("OTA", "OTA update successful; reboot should follow");
      break;
  }
}

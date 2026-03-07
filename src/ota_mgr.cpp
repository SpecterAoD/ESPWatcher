#include "ota_mgr.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>

#include "config.h"
#include "version.h"

namespace {
AppState* gState = nullptr;

#ifndef OTA_HOST
#define OTA_HOST NETDATA_HOST
#endif

#ifndef OTA_PORT
#define OTA_PORT 8091
#endif

#ifndef OTA_MANIFEST_PATH
#define OTA_MANIFEST_PATH "/manifest.json"
#endif

#ifndef OTA_FIRMWARE_PATH
#define OTA_FIRMWARE_PATH "/firmware.bin"
#endif

constexpr size_t MANIFEST_DOC_SIZE = 256;

int compareVersion(const char* current, const char* remote) {
  int currentIndex = 0;
  int remoteIndex = 0;

  while (current[currentIndex] != '\0' || remote[remoteIndex] != '\0') {
    int currentPart = 0;
    int remotePart = 0;

    while (current[currentIndex] >= '0' && current[currentIndex] <= '9') {
      currentPart = currentPart * 10 + (current[currentIndex++] - '0');
    }
    while (remote[remoteIndex] >= '0' && remote[remoteIndex] <= '9') {
      remotePart = remotePart * 10 + (remote[remoteIndex++] - '0');
    }

    if (currentPart != remotePart) {
      return currentPart < remotePart ? -1 : 1;
    }

    if (current[currentIndex] == '.') {
      currentIndex++;
    }
    if (remote[remoteIndex] == '.') {
      remoteIndex++;
    }
  }

  return 0;
}

void setStatus(const String& status) {
  if (gState != nullptr) {
    gState->otaStatus = status;
  }
}
}  // namespace

void otaSetup(AppState& state) {
  gState = &state;
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
  gState = &state;

  if (!state.wifiConnected || millis() - state.lastOtaCheckMs < OTA_CHECK_MS) {
    return;
  }

  state.lastOtaCheckMs = millis();

  if (!state.otaAutoEnabled) {
    Serial.println("[OTA] auto update disabled");
    setStatus("auto update disabled");
    return;
  }

  otaCheckForUpdate();
}

void otaCheckForUpdate() {
  Serial.println("[OTA] checking manifest");
  setStatus("checking manifest");

  WiFiClient client;
  HTTPClient http;

  String manifestUrl = String("http://") + OTA_HOST + ":" + OTA_PORT + OTA_MANIFEST_PATH;
  if (!http.begin(client, manifestUrl)) {
    Serial.println("[OTA] manifest request init failed");
    setStatus("manifest begin failed");
    return;
  }

  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(NETDATA_TIMEOUT_MS);

  const int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    Serial.printf("[OTA] manifest http status: %d\n", responseCode);
    setStatus("manifest unavailable");
    http.end();
    return;
  }

  StaticJsonDocument<MANIFEST_DOC_SIZE> doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.printf("[OTA] manifest parse failed: %s\n", err.c_str());
    setStatus("manifest parse failed");
    return;
  }

  const char* latest = doc["latest"] | "";
  const char* bin = doc["bin"] | "";
  const size_t size = doc["size"] | 0;
  const char* md5 = doc["md5"] | "";

  Serial.printf("[OTA] current version: %s\n", FW_VERSION);
  Serial.printf("[OTA] remote version: %s\n", latest);
  Serial.printf("[OTA] manifest bin: %s\n", bin);
  Serial.printf("[OTA] manifest size: %u\n", static_cast<unsigned int>(size));

  if (latest[0] == '\0') {
    Serial.println("[OTA] manifest missing latest version");
    setStatus("manifest missing latest");
    return;
  }

  if (compareVersion(FW_VERSION, latest) >= 0) {
    Serial.println("[OTA] already up-to-date");
    setStatus("up-to-date");
    return;
  }

  String firmwarePath = OTA_FIRMWARE_PATH;
  if (bin[0] != '\0') {
    firmwarePath = OTA_FIRMWARE_PATH;
    if (firmwarePath != String("/") + String(bin)) {
      Serial.println("[OTA] manifest bin differs from configured OTA path; using configured path");
    }
  }

  if (strlen(md5) == 32) {
    ESPhttpUpdate.setMD5sum(md5);
    Serial.println("[OTA] md5 provided; verification enabled");
  } else {
    ESPhttpUpdate.setMD5sum(nullptr);
    Serial.println("[OTA] md5 missing or invalid; skipping md5 verification");
  }

  otaDownloadFirmware(firmwarePath);
}

bool otaDownloadFirmware(String firmwarePath) {
  Serial.println("[OTA] downloading firmware");
  setStatus(String("downloading ") + firmwarePath);

  WiFiClient client;
  ESPhttpUpdate.rebootOnUpdate(false);

  const t_httpUpdate_return result = ESPhttpUpdate.update(client, OTA_HOST, OTA_PORT, firmwarePath.c_str(), FW_VERSION);

  switch (result) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("[OTA] update failed (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      setStatus(String("update failed: ") + ESPhttpUpdate.getLastErrorString());
      return false;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[OTA] no updates");
      setStatus("no updates");
      return false;

    case HTTP_UPDATE_OK:
      Serial.println("[OTA] update success");
      setStatus("update success");
      Serial.println("[OTA] rebooting");
      delay(250);
      ESP.restart();
      return true;
  }

  Serial.println("[OTA] unknown update result");
  setStatus("unknown update result");
  return false;
}

#include "wifi_mgr.h"

#include <ESP8266WiFi.h>
#include <time.h>

#include "config.h"
#include "logger.h"

namespace {
constexpr unsigned long WIFI_RETRY_MS = 10000UL;
constexpr unsigned long TIME_SYNC_MS = 30000UL;
bool lastConnected = false;
bool timeLogged = false;
unsigned long lastRetry = 0;

// Ist WIFI_SSID leer (oeffentlicher CI-Build ohne Zugangsdaten), nutzt der ESP die
// im Flash gespeicherten Daten. Sie bleiben bei einem OTA-Update erhalten.
bool hasCompiledCredentials() { return strlen(WIFI_SSID) > 0; }

void beginWifi() {
  if (hasCompiledCredentials()) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  } else {
    WiFi.begin();
  }
}

void updateNetworkState(AppState& state) {
  state.deviceIp = WiFi.localIP().toString();
  state.dnsServer = WiFi.dnsIP(0).toString();
}

void syncTime(AppState& state) {
  if (millis() - state.lastTimeSyncMs < TIME_SYNC_MS) {
    return;
  }
  state.lastTimeSyncMs = millis();

  time_t now = time(nullptr);
  if (now < 100000) {
    state.currentTime = "syncing";
    return;
  }

  struct tm tmValue;
  localtime_r(&now, &tmValue);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmValue);
  state.currentTime = buf;
  if (!timeLogged) {
    timeLogged = true;
    logMessage("NTP", String("Time synchronized: ") + state.currentTime);
  }
}
}  // namespace

void wifiSetup(AppState& state) {
  WiFi.persistent(true);  // Zugangsdaten im Flash merken (ueberleben OTA-Updates)
  WiFi.mode(WIFI_STA);
  WiFi.hostname("espwatcher");
  WiFi.setAutoReconnect(true);

  // Feste Adresse/DNS wird bewusst nicht gesetzt: DHCP vom Router genuegt, da
  // alle Ziele per IP angesprochen werden.
  beginWifi();
  configTime(TIME_ZONE, TIME_SERVER_HOST);

  state.wifiConnected = false;
  if (hasCompiledCredentials()) {
    logMessage("WIFI", String("Connecting to SSID '") + WIFI_SSID + "'");
  } else {
    logMessage("WIFI", "No credentials compiled in; using credentials stored in flash");
  }
  logMessage("NTP", String("Configured time server ") + TIME_SERVER_HOST);
}

void wifiLoop(AppState& state) {
  state.wifiConnected = WiFi.status() == WL_CONNECTED;

  if (state.wifiConnected) {
    if (!lastConnected) {
      updateNetworkState(state);
      logMessage("WIFI", String("Connected with IP ") + state.deviceIp);
      logMessage("WIFI", String("DNS server ") + state.dnsServer);
      state.currentTime = "syncing";
    }
    syncTime(state);
  } else if (lastConnected) {
    logMessage("WIFI", "Disconnected", LogLevel::WARN);
    state.deviceIp = "0.0.0.0";
    state.currentTime = "unavailable";
  }

  if (!state.wifiConnected && millis() - lastRetry > WIFI_RETRY_MS) {
    lastRetry = millis();
    logMessage("WIFI", "Retrying WiFi connect", LogLevel::WARN);
    WiFi.disconnect();
    beginWifi();
  }

  lastConnected = state.wifiConnected;
}

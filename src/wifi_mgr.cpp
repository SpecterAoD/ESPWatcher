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
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname("espwatcher");
  WiFi.setAutoReconnect(true);

  // Feste Adresse/DNS wird bewusst nicht gesetzt: DHCP vom Router genuegt, da
  // alle Ziele per IP angesprochen werden.
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(TIME_ZONE, TIME_SERVER_HOST);

  state.wifiConnected = false;
  logMessage("WIFI", String("Connecting to SSID '") + WIFI_SSID + "'");
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
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }

  lastConnected = state.wifiConnected;
}

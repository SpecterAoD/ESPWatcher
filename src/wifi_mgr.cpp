#include "wifi_mgr.h"

#include <ESP8266WiFi.h>
#include <time.h>

#include "config.h"
#include "logger.h"

namespace {
constexpr unsigned long WIFI_RETRY_MS = 10000UL;
constexpr unsigned long TIME_SYNC_MS = 60000UL;
bool lastConnected = false;
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
    logMessage("NTP", "Time not synchronized yet", LogLevel::WARN);
    return;
  }

  struct tm tmValue;
  localtime_r(&now, &tmValue);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmValue);
  state.currentTime = buf;
  logMessage("NTP", String("Time synchronized: ") + state.currentTime);
}
}  // namespace

void wifiSetup(AppState& state) {
  WiFi.mode(WIFI_STA);
  IPAddress dns;
  dns.fromString(DNS_SERVER_IP);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, dns);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(0, 0, TIME_SERVER_HOST);

  state.wifiConnected = false;
  state.dnsServer = DNS_SERVER_IP;
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

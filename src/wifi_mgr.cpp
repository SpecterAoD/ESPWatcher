#include "wifi_mgr.h"

#include <ESP8266WiFi.h>

#include "config.h"

void wifiSetup(AppState& state) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  state.wifiConnected = false;
}

void wifiLoop(AppState& state) {
  state.wifiConnected = WiFi.status() == WL_CONNECTED;

  static unsigned long lastRetry = 0;
  if (!state.wifiConnected && millis() - lastRetry > 10000) {
    lastRetry = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

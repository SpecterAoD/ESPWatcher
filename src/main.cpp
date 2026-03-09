#include <Arduino.h>

#include "config.h"
#include "health_eval.h"
#include "logger.h"
#include "netdata_client.h"
#include "ota_mgr.h"
#include "state.h"
#include "telemetry.h"
#include "version.h"
#include "webui.h"
#include "wifi_mgr.h"

AppState state;

void setup() {
  Serial.begin(115200);
  delay(100);

  logMessage("BOOT", "ESPWatcher boot start");
  logMessage("BOOT", String("Firmware version ") + FW_VERSION);

  wifiSetup(state);
  loggerSetup(SYSLOG_HOST, SYSLOG_PORT, "espwatcher");
  otaSetup(state);
  webSetup(state);

  logMessage("BOOT", "Startup sequence complete");
}

void loop() {
  wifiLoop(state);
  webLoop();
  otaLoop(state);

  if (!state.wifiConnected) {
    delay(20);
    return;
  }

  if (millis() - state.lastPollMs >= NETDATA_POLL_MS) {
    state.lastPollMs = millis();
    MetricPoint point;
    bool ok = netdataPoll(point);
    telemetryUpdate(state, point);
    state.systemHealth = evaluateHealth(state.latest);
    if (!ok) {
      logMessage("WARN", "Netdata poll failed", LogLevel::WARN);
    }
  }

  delay(5);
}

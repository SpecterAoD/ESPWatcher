#include <Arduino.h>

#include "config.h"
#include "health_eval.h"
#include "netdata_client.h"
#include "ota_mgr.h"
#include "state.h"
#include "telemetry.h"
#include "webui.h"
#include "wifi_mgr.h"

AppState state;

void setup() {
  Serial.begin(115200);
  delay(100);

  wifiSetup(state);
  otaSetup(state);
  webSetup(state);
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
    netdataPoll(point);
    telemetryUpdate(state, point);
    state.systemHealth = evaluateHealth(state.latest);
  }

  delay(5);
}

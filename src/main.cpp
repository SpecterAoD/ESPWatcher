#include <Arduino.h>

#include "config.h"
#include "health_eval.h"
#include "led.h"
#include "logger.h"
#include "netdata_client.h"
#include "ota_mgr.h"
#include "state.h"
#include "status_client.h"
#include "telemetry.h"
#include "version.h"
#include "webui.h"
#include "wifi_mgr.h"

AppState state;

namespace {
String lastReason;
bool firstEvaluation = true;

// Bewertet den Gesamtzustand neu und meldet Aenderungen per Syslog/Serial.
void refreshHealth() {
  String reason;
  state.systemHealth = evaluateHealth(state.latest, state.status, &reason);
  state.healthReason = reason.length() ? reason : String(healthToGerman(HealthState::GREEN));

  if (firstEvaluation || reason != lastReason) {
    firstEvaluation = false;
    lastReason = reason;
    const LogLevel level = state.systemHealth == HealthState::RED      ? LogLevel::ERR
                           : state.systemHealth == HealthState::YELLOW ? LogLevel::WARN
                                                                       : LogLevel::INFO;
    logMessage("HEALTH", String(healthToText(state.systemHealth)) + ": " + state.healthReason, level);
    telemetryEvent(state, static_cast<uint8_t>(state.systemHealth) + 1, state.healthReason);
  }
}

void pollStatus() {
  static StatusPoint point;  // nicht auf dem Stack: der ESP8266 hat nur ca. 4 KB davon
  if (statusPoll(point)) {
    state.status = point;
  } else {
    // Letzte bekannte Werte behalten, aber als nicht erreichbar markieren.
    state.status.reachable = false;
  }
}

void pollNetdata() {
  MetricPoint point;
  netdataPoll(point);

  // Fehlende Werte aus dem Status-JSON ergaenzen (z. B. Temperatur).
  if (state.status.reachable) {
    if (isnan(point.temperature)) point.temperature = state.status.cpuTemp;
    if (isnan(point.load1)) point.load1 = state.status.load1;
    if (isnan(point.ramUsage)) point.ramUsage = state.status.memUsedPct;
  }

  telemetryUpdate(state, point);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  ledSetup();

  logMessage("BOOT", "ESPWatcher boot start");
  logMessage("BOOT", String("Firmware version ") + FW_VERSION);

  wifiSetup(state);
  loggerSetup(SYSLOG_HOST, SYSLOG_PORT, "espwatcher");
  otaSetup(state);
  webSetup(state);

  // Erste Abfrage sofort nach dem WLAN-Start statt erst nach dem Intervall.
  state.lastStatusPollMs = millis() - STATUS_POLL_MS;
  state.lastPollMs = millis() - NETDATA_POLL_MS;

  logMessage("BOOT", "Startup sequence complete");
}

void loop() {
  wifiLoop(state);
  webLoop();
  otaLoop(state);
  ledLoop(state.systemHealth, state.wifiConnected);

  if (!state.wifiConnected) {
    delay(20);
    return;
  }

  if (millis() - state.lastStatusPollMs >= STATUS_POLL_MS) {
    state.lastStatusPollMs = millis();
    pollStatus();
    refreshHealth();
  }

  if (millis() - state.lastPollMs >= NETDATA_POLL_MS) {
    state.lastPollMs = millis();
    pollNetdata();
    refreshHealth();
  }

  if (millis() - state.lastHistoryMs >= HISTORY_STEP_MS) {
    state.lastHistoryMs = millis();
    telemetrySample(state);
  }

  delay(5);
}

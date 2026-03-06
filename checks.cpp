#include "checks.h"
#include "config.h"
#include "reset_pi.h"
#include <ESP8266WiFi.h>

static uint32_t lastMs = 0;
static uint32_t piDownSinceMs = 0;

static uint32_t lastScanMs = 0;
static bool lastScanHit = false;

// Scan wirklich selten! (z.B. alle 60s)
static const uint32_t SSID_SCAN_INTERVAL_MS = 60000;

static bool tcpCheck(const IPAddress& ip, uint16_t port, uint16_t timeoutMs=300) {
  WiFiClient c;
  c.setTimeout(timeoutMs);
  bool ok = c.connect(ip, port);
  c.stop();
  return ok;
}

// ACHTUNG: Scan ist teuer und blockiert -> nur selten aufrufen!
static bool scanForSsidSlow(const char* ssidNeedle) {
  int n = WiFi.scanNetworks(false, true);
  if (n <= 0) return false;
  for (int i=0;i<n;i++) {
    yield(); // WDT füttern
    String ssid = WiFi.SSID(i);
    if (ssid == ssidNeedle) return true;
  }
  return false;
}

void checksInit(State& s) {
  for (size_t i=0;i<TOPIC_COUNT;i++) {
    s.topics[i].topic = TOPIC_LIST[i];
    s.topics[i].value = "";
    s.topics[i].lastMs = 0;
  }
}

void checksLoop(State& s) {
  if (millis() - lastMs < CHECK_EVERY_MS) return;
  lastMs = millis();

  // Pi checks
  s.piSsh   = tcpCheck(PI_IP, PI_PORT_SSH);
  s.piDns   = tcpCheck(PI_IP, PI_PORT_DNS);
  s.piHttps = tcpCheck(PI_IP, PI_PORT_HTTPS);
  s.piMqtt  = tcpCheck(PI_IP, PI_PORT_MQTT);

  s.piReachable = (s.piSsh || s.piDns || s.piHttps || s.piMqtt);

  // ESP-link HTTP
  s.esplinkHttp = tcpCheck(ESPLINK_IP, ESPLINK_HTTP_PORT);

  // SSID scan NUR selten und nur wenn HTTP down (sonst unnötig)
  if (!s.esplinkHttp) {
    if (millis() - lastScanMs > SSID_SCAN_INTERVAL_MS) {
      lastScanMs = millis();
      lastScanHit = scanForSsidSlow(ESPLINK_SSID);
      WiFi.scanDelete(); // RAM frei machen
    }
    s.esplinkSsidSeen = lastScanHit;
  } else {
    s.esplinkSsidSeen = false; // wenn http ok, nicht wichtig
  }

  // Auto-reset logic (optional)
  if (!s.piReachable) {
    if (piDownSinceMs == 0) piDownSinceMs = millis();
    if (AUTO_RESET_ENABLE && (millis() - piDownSinceMs > AUTO_RESET_AFTER_MS)) {
      resetPiPulse(600);
      piDownSinceMs = 0;
    }
  } else {
    piDownSinceMs = 0;
  }

  // Drift: nur wenn beide Zeiten da
  if (s.ntpOk && s.espUnix > 1700000000UL && s.piUnix > 1700000000UL) {
    int32_t d = (int32_t)s.espUnix - (int32_t)s.piUnix;
    s.driftSec = (float)d;
    float ad = fabs(s.driftSec);
    if (ad >= DRIFT_CRIT_S) s.driftLevel = 2;
    else if (ad >= DRIFT_WARN_S) s.driftLevel = 1;
    else s.driftLevel = 0;
  } else {
    s.driftSec = 0.0f;
    s.driftLevel = 2;
  }

  yield();
}

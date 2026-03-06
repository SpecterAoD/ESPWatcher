#include "ntp_check.h"
#include "config.h"

#include <Arduino.h>
#include <time.h>

// ESP8266: configTime() + time(nullptr) ist der robuste Weg.
// Wir nutzen den Pi als NTP-Server (NTP_IP aus config.h)

static uint32_t lastSyncTryMs = 0;
static bool timeConfigured = false;

static void configureTimeOnce() {
  if (timeConfigured) return;
  timeConfigured = true;

  // TZ: Deutschland (CET/CEST)
  // ESP8266 nutzt POSIX TZ string
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
  tzset();

  // NTP Server: Pi
  const char* ntp = NTP_IP.toString().c_str();
  configTime(0, 0, ntp, "pool.ntp.org"); // fallback optional
}

static uint32_t getUnix() {
  time_t now = time(nullptr);
  if (now < 100000) return 0; // noch nicht synchron
  return (uint32_t)now;
}

void ntpInit() {
  timeConfigured = false;
  lastSyncTryMs = 0;
  configureTimeOnce();
}

void ntpLoop(State& s) {
  configureTimeOnce();

  // Alle NTP_SYNC_MS einen Sync-Check (kein harter sync call nötig,
  // configTime läuft im Hintergrund; wir markieren nur "ok" wenn Zeit plausibel ist)
  if (millis() - lastSyncTryMs >= NTP_SYNC_MS) {
    lastSyncTryMs = millis();
  }

  uint32_t espNow = getUnix();
  if (espNow != 0) {
    s.ntpOk = true;
    s.ntpLastSyncMs = millis();
    s.espUnix = espNow;
  } else {
    s.ntpOk = false;
    // espUnix nicht überschreiben -> sonst Drift springt
  }

  // Drift nur berechnen wenn beide Werte da sind
  if (s.ntpOk && s.piUnix != 0) {
    // Achtung: piUnix kommt über MQTT (pi/meta/unix)
    s.driftSec = (float)((int32_t)s.espUnix - (int32_t)s.piUnix);

    float ad = fabsf(s.driftSec);
    if (ad >= DRIFT_CRIT_S) s.driftLevel = 2;
    else if (ad >= DRIFT_WARN_S) s.driftLevel = 1;
    else s.driftLevel = 0;
  } else {
    // solange kein Vergleich möglich -> kritisch anzeigen
    s.driftLevel = 2;
    // driftSec kann bleiben
  }
}

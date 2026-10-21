#include "wifi_mgr.h"

#include <ESP8266WiFi.h>
#include <time.h>

#include "config.h"
#include "logger.h"

namespace {
constexpr unsigned long WIFI_RETRY_MS = 30000UL;  // genug Zeit fuer den Verbindungsaufbau
constexpr unsigned long TIME_SYNC_MS = 30000UL;
bool lastConnected = false;
WiFiEventHandler disconnectHandler;
volatile uint8_t lastReason = 0;
volatile bool reasonPending = false;
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

const char* reasonText(uint8_t reason) {
  switch (reason) {
    case 2: return "Anmeldung abgelaufen (AUTH_EXPIRE)";
    case 15: case 204: return "4-Wege-Handshake fehlgeschlagen: Passwort falsch";
    case 6: case 7: return "Router lehnt Gerät ab (Frame von unbekanntem Gerät)";
    case 201: return "Netz nicht gefunden";
    case 202: return "Anmeldung abgelehnt (AUTH_FAIL): Passwort oder Verschlüsselung (WPA3?)";
    case 203: return "Zuordnung fehlgeschlagen (ASSOC_FAIL): Router voll oder Zugriffsliste";
    case 205: return "Verbindung fehlgeschlagen";
    default: return "anderer Grund";
  }
}

const char* statusText(wl_status_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL: return "SSID nicht gefunden (Name falsch, 5 GHz oder zu weit weg)";
    case WL_CONNECT_FAILED: return "Anmeldung abgelehnt (Passwort oder Verschluesselung)";
    case WL_CONNECTION_LOST: return "Verbindung verloren";
    case WL_WRONG_PASSWORD: return "falsches Passwort";
    case WL_DISCONNECTED: return "nicht verbunden";
    case WL_IDLE_STATUS: return "verbindet gerade";
    default: return "unbekannt";
  }
}

// Zeigt, welche Netze der ESP sieht (ohne Passwoerter), und ob das gesuchte dabei ist.
void logScan() {
  String target = hasCompiledCredentials() ? String(WIFI_SSID) : WiFi.SSID();
  int count = WiFi.scanNetworks();
  if (count < 0) {
    logMessage("WIFI", "Scan fehlgeschlagen", LogLevel::WARN);
    return;
  }
  bool found = false;
  for (int i = 0; i < count; i++) {
    if (WiFi.SSID(i) == target) found = true;
    if (i < 8) {
      logMessage("WIFI", String("  sichtbar: '") + WiFi.SSID(i) + "' Kanal " + WiFi.channel(i) + " " +
                             WiFi.RSSI(i) + " dBm" + (WiFi.encryptionType(i) == ENC_TYPE_NONE ? " offen" : ""));
    }
  }
  logMessage("WIFI", String("Gesuchtes Netz '") + target + "' " + (found ? "ist sichtbar" : "wird NICHT gesehen"),
             found ? LogLevel::INFO : LogLevel::WARN);
  WiFi.scanDelete();
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
  disconnectHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& event) {
    lastReason = event.reason;
    reasonPending = true;
  });
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

  if (reasonPending) {
    reasonPending = false;
    logMessage("WIFI", String("Trennungsgrund ") + lastReason + ": " + reasonText(lastReason), LogLevel::WARN);
  }

  if (!state.wifiConnected && millis() - lastRetry > WIFI_RETRY_MS) {
    lastRetry = millis();
    static uint8_t attempts = 0;
    logMessage("WIFI", String("Retrying WiFi connect: ") + statusText(WiFi.status()), LogLevel::WARN);
    if (attempts++ % 6 == 0) {
      logScan();
    }
    WiFi.disconnect();
    beginWifi();
  }

  lastConnected = state.wifiConnected;
}

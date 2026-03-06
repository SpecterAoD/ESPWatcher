#include "wifi_mgr.h"
#include "config.h"
#include <ESP8266WiFi.h>

enum ModeState { STA_TRY, AP_ONLY };
static ModeState modeState = STA_TRY;

static uint32_t stateStartMs = 0;

static const uint32_t STA_TRY_MS = 25000;   // 25s STA try
static const uint32_t AP_ONLY_MS = 60000;   // 60s AP only

static bool ipReady() {
  IPAddress ip = WiFi.localIP();
  return (ip[0] != 0); // 0.0.0.0 => nicht bereit
}

static void enterStaTry(State& s) {
  Serial.println("\n=== ENTER STA_TRY (AP OFF) ===");

  // AP sicher aus
  WiFi.softAPdisconnect(true);
  s.apUp = false;

  WiFi.persistent(false);

  // Stabilität (Mesh): Sleep aus, AutoReconnect an
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoReconnect(true);

  // Optional (oft stabiler beim ESP8266):
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);

  WiFi.mode(WIFI_STA);

  // Sanfter Reset (dein Wunsch: nicht disconnect(true))
  WiFi.disconnect();
  delay(150);

  Serial.printf("Connecting to %s ...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  modeState = STA_TRY;
  stateStartMs = millis();
}

static void enterApOnly(State& s) {
  if (!ENABLE_FALLBACK_AP) {
    enterStaTry(s);
    return;
  }

  Serial.println("\n=== ENTER AP_ONLY (STA OFF) ===");

  // STA sauber beenden
  WiFi.disconnect();
  delay(100);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255,255,255,0));

  bool ok = WiFi.softAP(AP_SSID, AP_PASS);
  s.apUp = ok;

  Serial.printf("Fallback-AP: %s ok=%d IP=%s\n",
                AP_SSID, (int)ok, WiFi.softAPIP().toString().c_str());

  modeState = AP_ONLY;
  stateStartMs = millis();
}

void wifiInit(State& s) {
  s.staUp = false;
  s.apUp = false;
  s.gotIp = false;
  s.gotIpMs = 0;

  // Events: nur zum Zurücksetzen/Loggen
  WiFi.onStationModeDisconnected([&s](const WiFiEventStationModeDisconnected& e){
    s.staUp = false;
    s.gotIp = false;
    Serial.printf("!!! DISCONNECTED reason=%d\n", e.reason);
  });

  WiFi.onStationModeGotIP([&s](const WiFiEventStationModeGotIP& e){
    s.staUp = true;
    s.gotIp = true;
    s.gotIpMs = millis();
    Serial.printf(">>> GOT IP event: %s GW=%s\n",
                  e.ip.toString().c_str(),
                  e.gw.toString().c_str());
  });

  enterStaTry(s);
}

void wifiLoop(State& s) {
  wl_status_t st = WiFi.status();
  bool staConnected = (st == WL_CONNECTED);
  bool hasIp = ipReady();

  s.staUp = staConnected;

  // gotIp robust setzen
  if (staConnected && hasIp) {
    if (!s.gotIp) {
      s.gotIp = true;
      s.gotIpMs = millis();
      Serial.printf(">>> ONLINE (loop) IP=%s GW=%s DNS=%s\n",
                    WiFi.localIP().toString().c_str(),
                    WiFi.gatewayIP().toString().c_str(),
                    WiFi.dnsIP().toString().c_str());
    }
  } else {
    // Nur zurücksetzen wenn wirklich nicht mehr "online"
    if (s.gotIp) {
      s.gotIp = false;
      Serial.printf("<<< OFFLINE (loop) status=%d ip=%s\n",
                    (int)st,
                    WiFi.localIP().toString().c_str());
    }
  }

  // Logging
  static uint32_t lastLog = 0;
  if (millis() - lastLog > 3000) {
    lastLog = millis();
    Serial.printf("%s... status=%d ip=%s gotIp=%d\n",
                  (modeState==STA_TRY ? "STA_TRY" : "AP_ONLY"),
                  (int)st,
                  WiFi.localIP().toString().c_str(),
                  (int)s.gotIp);
  }

  if (modeState == STA_TRY) {
    // Wenn online: fertig
    if (s.gotIp) return;

    // >>> WICHTIGER FIX:
    // Timeout basiert auf gotIp, NICHT auf staUp
    if (millis() - stateStartMs > STA_TRY_MS) {
      enterApOnly(s);
    }
  } else { // AP_ONLY
    // Nach Zeit wieder STA versuchen
    if (millis() - stateStartMs > AP_ONLY_MS) {
      enterStaTry(s);
    }
  }
}

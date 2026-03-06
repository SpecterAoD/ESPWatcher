#include "config.h"
#include "state.h"
#include "wifi_mgr.h"
#include "ota_mgr.h"
#include "webui.h"
#include "checks.h"
#include "mqtt_mgr.h"
#include "reset_pi.h"
#include "adc_mgr.h"
#include "ota_http.h"
#include "version.h"
#include "ntp_check.h"

State g;

void setup() {
  Serial.begin(115200);
  delay(200);

  g.bootMs = millis();

  resetPiInit();

  // WLAN zuerst!
  wifiInit(g);

  otaHttpInit(g);
  ntpInit();
  adcInit(g);

  // Diese dürfen auch im AP laufen
  otaInit();
  webInit(g);

  // MQTT/Checks initialisieren, aber laufen erst wenn gotIp==true
  mqttInit(g);
  checksInit(g);

  Serial.println("Boot done (wifi stage)");
}

void loop() {
  wifiLoop(g);
  webLoop();
  otaLoop();
  otaHttpLoop(g);
  adcLoop(g);
  ntpLoop(g);
  // Checks brauchen WLAN nicht zwingend, aber sinnvoll erst wenn IP da ist
  if (g.gotIp) {
    checksLoop(g);
    mqttLoop(g);
  } else {
    delay(10);
  }
}

#include "ota_mgr.h"
#include "config.h"
#include <ArduinoOTA.h>

void otaInit() {
  ArduinoOTA.setHostname("rescue-monitor");
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.begin();

}

void otaLoop() {
  ArduinoOTA.handle();
}

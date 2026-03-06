#include "mqtt_mgr.h"
#include "config.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);
static State* gS = nullptr;

static int topicIndex(const String& t) {
  for (size_t i = 0; i < TOPIC_COUNT; i++) {
    if (gS->topics[i].topic == t) return (int)i;
  }
  return -1;
}

static void cb(char* topic, byte* payload, unsigned int len) {
  if (!gS) return;

  String t(topic);
  String v;
  v.reserve(len+1);
  for (unsigned int i=0;i<len;i++) v += (char)payload[i];

  // topic cache
  int idx = topicIndex(t);
  if (idx >= 0) {
    gS->topics[idx].value = v;
    gS->topics[idx].lastMs = millis();
  }

  // Spezial: pi/meta/unix -> drift basis
  if (t == "pi/meta/unix") {
    gS->piUnix = (uint32_t)v.toInt();
    gS->piUnixLastMs = millis();
  }

  // mqtt ok
  gS->mqttConnected = true;
  gS->mqttLastOkMs = millis();

  // optional sparklines etc.
  if (t == "pi/health/temp_cpu") {
    gS->sp_cpuTemp.push(v.toFloat());
  }
}

void mqttInit(State& s) {
  gS = &s;
  mqtt.setServer(PI_IP, PI_PORT_MQTT);
  mqtt.setCallback(cb);
}

static void subscribeAll() {
  // du kannst auch "pi/#" nehmen, aber wir bleiben bei TOPIC_LIST
  for (size_t i=0;i<TOPIC_COUNT;i++) {
    mqtt.subscribe(TOPIC_LIST[i], 1);
  }
}

bool mqttPublish(const char* topic, const char* payload, bool retain) {
  if (!mqtt.connected()) return false;
  return mqtt.publish(topic, payload, retain);
}

void mqttLoop(State& s) {
  if (!MQTT_ENABLE) return;
  if (WiFi.status() != WL_CONNECTED) { s.mqttConnected = false; return; }

  if (!mqtt.connected()) {
    s.mqttConnected = false;

    String cid = String(MQTT_CLIENT_ID) + "-" + String(ESP.getChipId(), HEX);
    bool ok = mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS);
    if (ok) {
      s.mqttConnected = true;
      s.mqttLastOkMs = millis();
      subscribeAll();
    }
  } else {
    mqtt.loop();
    s.mqttConnected = true;
  }
}

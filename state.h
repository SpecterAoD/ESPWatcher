#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "config.h"

struct TopicValue {
  String topic;
  String value;
  uint32_t lastMs = 0;
};

struct Spark {
  static const uint8_t N = 40;
  float v[N] = {0};
  uint8_t head = 0;
  bool filled = false;

  void push(float x) {
    v[head] = x;
    head = (head + 1) % N;
    if (head == 0) filled = true;
  }
};

struct State {
  uint32_t bootMs = 0;

  // Wifi
  bool staUp = false;
  bool apUp  = false;

  bool gotIp = false;
  uint32_t gotIpMs = 0;

  // Checks
  bool piReachable = false;
  bool piSsh = false;
  bool piDns = false;
  bool piHttps = false;
  bool piMqtt = false;

  // ESP-link (#1) Liveness
  bool esplinkHttp = false;
  bool esplinkSsidSeen = false;

  // ---------------- OTA HTTP ----------------
  String fwLocal;        // lokale FW_VERSION
  String fwRemote;       // manifest.latest
  String fwRemoteBin;    // manifest.bin
  String fwRemoteMd5;    // manifest.md5
  uint32_t fwRemoteSize = 0;

  bool otaUpdateAvailable = false;
  bool otaBusy = false;
  String otaLastMsg = "";
  uint32_t otaLastCheckMs = 0;

  bool otaDoUpdate = false;
  String otaRequestedVer = ""; // "" oder "latest" => latest

  // Progress
  uint8_t  otaPct = 0;
  uint32_t otaBytesCur = 0;
  uint32_t otaBytesTotal = 0;

  // OTA state machine (non-blocking)
  enum OtaStage : uint8_t {
    OTA_IDLE=0,
    OTA_PREPARE=1,
    OTA_DOWNLOADING=2,
    OTA_ERROR=3
  };
  uint8_t otaStage = OTA_IDLE;

  // Rollback list (versions.json)
  static const uint8_t OTA_MAX_ITEMS = 24;
  struct OtaItem {
    String ver;
    String bin;
    String md5;
    uint32_t size;
  };
  uint8_t otaItemCount = 0;
  OtaItem otaItems[OTA_MAX_ITEMS];

  // ---------------- NTP / Drift (ESP Anzeige) ----------------
  bool ntpOk = false;
  uint32_t ntpLastSyncMs = 0;

  uint32_t espUnix = 0;        // ESP epoch seconds (NTP)
  uint32_t piUnix  = 0;        // Pi epoch seconds (MQTT pi/meta/unix)
  uint32_t piUnixLastMs = 0;   // wann zuletzt piUnix empfangen (millis)

  float driftSec = 0.0f;       // espUnix - piUnix
  uint8_t driftLevel = 2;      // 0 OK, 1 WARN, 2 CRIT

  // ---------------- Pi RTC Drift (Anzeige) ----------------
  uint32_t rtcUnix = 0;        // RTC epoch seconds (vom Pi)
  long rtcDriftSec = 0;        // Drift in Sekunden (System - RTC)
  uint8_t rtcLevel = 2;        // 0 OK, 1 WARN, 2 CRIT

  // MQTT
  bool mqttConnected = false;
  uint32_t mqttLastOkMs = 0;

  // MQTT topic cache
  TopicValue topics[TOPIC_COUNT];

  // ADC 5V Messung (ESP)
  bool adsOk = false;
  float v5_pi  = 0.0f;
  float v5_hub = 0.0f;
  uint32_t v5_lastMs = 0;

  // Sparklines
  Spark sp_cpuTemp;
  Spark sp_v5pi;
  Spark sp_v5hub;
};

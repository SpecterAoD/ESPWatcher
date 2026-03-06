// ======================= config.h =======================
#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>

// -------- WLAN (Heimnetz) ----------
static const char* WIFI_SSID     = "WIFISSID";
static const char* WIFI_PASS     = "WIFIPASS";

// Optional: eigene feste IP (nur wenn du willst). Sonst DHCP.
static const bool  USE_STATIC_IP = false;
static const IPAddress STA_IP(192,168,2,30);
static const IPAddress STA_GW(192,168,2,1);
static const IPAddress STA_MASK(255,255,255,0);
static const IPAddress STA_DNS(192,168,2,6);

// -------- Fallback-AP (Notfall) ----------
static const bool  ENABLE_FALLBACK_AP = true;
static const char* AP_SSID = "RescueConsole";
static const char* AP_PASS = "RescueConsole"; // min 8 Zeichen
static const IPAddress AP_IP(192,168,5,1);

// -------- Ziele (Pi / Dienste) ----------
static const IPAddress PI_IP(192,168,2,6);
static const uint16_t PI_PORT_SSH   = 22;
static const uint16_t PI_PORT_DNS   = 53;
static const uint16_t PI_PORT_HTTPS = 443;
static const uint16_t PI_PORT_MQTT  = 1883;

// -------- ESP-link (#1) Liveness ----------
static const char* ESPLINK_SSID = "ESP_RESCUE";           // wenn als AP sichtbar
static const IPAddress ESPLINK_IP(192,168,2,16);          // wenn im Heim-WLAN (empfohlen)
static const uint16_t ESPLINK_HTTP_PORT = 80;

// --- OTA HTTP Server (Pi) ---
// python -m http.server 8090 --directory /mnt/data/ota
// Dateien liegen unter /mnt/data/ota/firmwares/*
static const char* OTA_BASE_URL      = "http://192.168.2.6:8090";
static const uint16_t OTA_PORT       = 8090;
static const char* OTA_PASS          = "OTAesp8266";

// Wichtig: manifest/versions liegen unter /firmwares/
static const char* OTA_FIRMWARE_DIR  = "/firmwares";
static const char* OTA_MANIFEST      = "/firmwares/manifest.json";
static const char* OTA_VERSIONS      = "/firmwares/versions.json";
static const uint32_t OTA_CHECK_INTERVAL = 60UL * 1000UL; // 60s

// --- NTP / Drift ---
static const IPAddress NTP_IP(192,168,2,6);
static const uint32_t NTP_SYNC_MS = 5UL * 60UL * 1000UL; // 5 min

// Drift-Grenzen (ESP-Zeit vs Pi-Unix)
static const float DRIFT_WARN_S = 2.0f;
static const float DRIFT_CRIT_S = 5.0f;

// RTC Drift Grenzen (Pi Systemzeit vs Pi RTC) -> Anzeige nur, Alarm macht der Pi
static const long RTC_WARN_S = 5;
static const long RTC_CRIT_S = 20;

// -------- Reset-Pin (D1 mini) ----------
static const uint8_t PIN_RESET_PI = D5; // GPIO14 -> NPN -> RUN

// -------- Monitoring / Interval ----------
static const uint32_t CHECK_EVERY_MS = 3000;
static const uint32_t WIFI_RETRY_MS  = 20000;

// -------- MQTT (Broker auf Pi) ----------
static const bool MQTT_ENABLE = true;
static const char* MQTT_USER = "espmon";
static const char* MQTT_PASS = "esp8266";
static const char* MQTT_CLIENT_ID = "rescue-monitor";

// -------- Topics ----------
static const char* TOPIC_LIST[] = {
  // meta
  "pi/meta/host",
  "pi/meta/time",
  "pi/meta/unix",

  // RTC
  "pi/meta/rtc_unix",
  "pi/meta/rtc_drift",

  // Netzwerk Clients (CSV)
  //"pi/net/clients",

  // health
  "pi/health/temp_cpu",
  "pi/health/load1",
  "pi/health/load5",
  "pi/health/load15",
  "pi/health/mem_avail_kb",
  "pi/health/mem_used_kb",

  // power
  "pi/power/throttled_hex",
  "pi/power/undervoltage_now",
  "pi/power/undervoltage_past",
  "pi/power/armcap_now",
  "pi/power/armcap_past",
  "pi/power/throttled_now",
  "pi/power/throttled_past",
  "pi/power/temp_limit_now",
  "pi/power/temp_limit_past",
  "pi/power/vcore",
  "pi/power/vsdram_c",

  // storage
  "pi/storage/data_total_kb",
  "pi/storage/data_avail_kb",
  "pi/storage/data_use_percent",

  // services
  "pi/service/pihole_ftl",
  "pi/service/mosquitto"
};
static const size_t TOPIC_COUNT = sizeof(TOPIC_LIST)/sizeof(TOPIC_LIST[0]);

// -------- ADS1115 5V Messung ----------
static const bool ENABLE_ADS1115 = true;

// I2C pins für D1 mini
static const uint8_t PIN_I2C_SCL = D1;
static const uint8_t PIN_I2C_SDA = D2;

// ADS1115 Adresse (ADDR=GND)
static const uint8_t ADS1115_ADDR = 0x48;

// Kanäle
static const uint8_t ADS_CH_5V_PI  = 0; // A0
static const uint8_t ADS_CH_5V_HUB = 1; // A1

// Spannungsteiler
// 100k/100k -> Faktor 2.0 (5V -> 2.5V am ADC)
static const float DIV_RTOP_OHM = 100000.0f;
static const float DIV_RBOT_OHM = 100000.0f;

// Warn/Crit Schwellen
static const float V5_WARN = 4.90f;
static const float V5_CRIT = 4.80f;

// CPU Kerne (Pi3=4)
static const int CPU_CORES = 4;

// -------- Auto-Reset (optional) ----------
static const bool AUTO_RESET_ENABLE = false;
static const uint32_t AUTO_RESET_AFTER_MS = 10UL * 60UL * 1000UL; // 10 min tot -> reset



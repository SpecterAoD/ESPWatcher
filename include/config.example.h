#pragma once

// Kopie dieser Datei als include/config.local.h anlegen und Werte eintragen.
// Alle Zeilen unter "optional" duerfen weggelassen werden (Standardwerte in config.h).

// Beim ersten Flashen per USB eintragen. Der ESP speichert die Daten im Flash; spaetere
// Builds (auch der oeffentliche GitHub-Build mit leeren Werten) nutzen sie weiter.
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// piholepi (Netdata, Syslog, Zeitserver und Status-JSON laufen dort)
#define NETDATA_HOST "192.168.2.6"

// ---- optional --------------------------------------------------------------
// #define PIZERO_HOST "192.168.2.5"
// #define DNS_VIP "192.168.2.10"          // keepalived-Adresse

// #define STATUS_PORT 8092                // Status-JSON (pi/espwatcher-status.py)
// #define STATUS_POLL_MS 10000UL

// #define NETDATA_TEMP_CHART "sensors.temp"
// #define NETDATA_POLL_MS 10000UL

// OTA nur aktivieren, wenn dort ein Manifest bereitliegt:
// #define OTA_HOST "192.168.2.6"
// #define OTA_PORT 8091
// #define OTA_MANIFEST_PATH "/manifest.json"
// #define OTA_AUTO_UPDATE_DEFAULT 0

// #define SYSLOG_HOST "192.168.2.6"
// #define SYSLOG_PORT 514
// #define TIME_SERVER_HOST "192.168.2.6"

// LED-Ampel (GPIO-Nummern; D5=14, D6=12, D7=13) oder abschalten:
// #define LED_ENABLED 0
// #define LED_GREEN_PIN 14
// #define LED_YELLOW_PIN 12
// #define LED_RED_PIN 13

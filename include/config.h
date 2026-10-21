#pragma once

#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#warning "Using config.example.h. Create include/config.local.h for real deployment."
#endif

// WLAN-Zugangsdaten duerfen leer sein: Dann nutzt der ESP die im Flash gespeicherten
// Daten (so entsteht der oeffentliche CI-Build ohne Passwort in der Firmware).
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

// ---- Zielsysteme im Heimnetz -------------------------------------------
#ifndef PIHOLEPI_HOST
#define PIHOLEPI_HOST NETDATA_HOST
#endif

#ifndef PIZERO_HOST
#define PIZERO_HOST "192.168.2.5"
#endif

// Keepalived-Adresse (virtuelle DNS-IP)
#ifndef DNS_VIP
#define DNS_VIP "192.168.2.10"
#endif

// ---- Status-JSON vom piholepi (pi/espwatcher-status.py) -----------------
#ifndef STATUS_HOST
#define STATUS_HOST PIHOLEPI_HOST
#endif

#ifndef STATUS_PORT
#define STATUS_PORT 8092
#endif

#ifndef STATUS_PATH
#define STATUS_PATH "/status.json"
#endif

#ifndef STATUS_TIMEOUT_MS
#define STATUS_TIMEOUT_MS 3000
#endif

#ifndef STATUS_POLL_MS
#define STATUS_POLL_MS 10000UL
#endif

// Status-Daten aelter als dies gelten als veraltet (Sekunden)
#ifndef STATUS_STALE_S
#define STATUS_STALE_S 45
#endif

// ---- Netdata -------------------------------------------------------------
#ifndef NETDATA_PORT
#define NETDATA_PORT 19999
#endif

#ifndef NETDATA_TIMEOUT_MS
#define NETDATA_TIMEOUT_MS 2500
#endif

#ifndef NETDATA_POLL_MS
#define NETDATA_POLL_MS 10000UL
#endif

// Chart fuer die CPU-Temperatur; Name pruefen mit:
// curl -s http://<piholepi>:19999/api/v1/charts | grep -io '"sensors[^"]*"'
// Ohne passenden Chart nutzt die Firmware die Temperatur aus dem Status-JSON.
// Netdata-Warnungen, deren Name diesen Text enthaelt, werden ignoriert (kritische nie). Leer = nichts ignorieren.
#ifndef NETDATA_IGNORE_WARN
#define NETDATA_IGNORE_WARN "tailscale0"
#endif

#ifndef NETDATA_TEMP_CHART
#define NETDATA_TEMP_CHART "sensors.temp"
#endif

// ---- OTA, Syslog, Zeit -----------------------------------------------------
// OTA ist nur aktiv, wenn OTA_HOST in config.local.h gesetzt ist.
#ifdef OTA_HOST
#define OTA_CONFIGURED 1
#else
#define OTA_CONFIGURED 0
#define OTA_HOST NETDATA_HOST
#endif

#ifndef OTA_PORT
#define OTA_PORT 8091
#endif

#ifndef OTA_MANIFEST_PATH
#define OTA_MANIFEST_PATH "/manifest.json"
#endif

#ifndef OTA_CHECK_MS
#define OTA_CHECK_MS 600000UL
#endif

#ifndef OTA_AUTO_UPDATE_DEFAULT
#define OTA_AUTO_UPDATE_DEFAULT 0
#endif

#ifndef SYSLOG_HOST
#define SYSLOG_HOST NETDATA_HOST
#endif

#ifndef SYSLOG_PORT
#define SYSLOG_PORT 514
#endif

#ifndef TIME_SERVER_HOST
#define TIME_SERVER_HOST NETDATA_HOST
#endif

// Zeitzone (POSIX-Format), Standard: Mitteleuropa mit Sommerzeit
#ifndef TIME_ZONE
#define TIME_ZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

#ifndef DNS_SERVER_IP
#define DNS_SERVER_IP NETDATA_HOST
#endif

// ---- LED-Ampel (Wemos D1 mini: D5=GPIO14, D6=GPIO12, D7=GPIO13) ----------
// Je LED einen 220-330 Ohm Vorwiderstand gegen GND verwenden.
#ifndef LED_ENABLED
#define LED_ENABLED 1
#endif

#ifndef LED_GREEN_PIN
#define LED_GREEN_PIN 14
#endif

#ifndef LED_YELLOW_PIN
#define LED_YELLOW_PIN 12
#endif

#ifndef LED_RED_PIN
#define LED_RED_PIN 13
#endif

#pragma once

// Copy this file to config.local.h and fill with your values.
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"
#define NETDATA_HOST "192.168.2.6"

#define NETDATA_PORT 19999
#define NETDATA_TIMEOUT_MS 2500
#define NETDATA_POLL_MS 5000

#define OTA_MANIFEST_PATH "/firmware/manifest.json"
#define OTA_CHECK_MS 600000UL
#define OTA_AUTO_UPDATE_DEFAULT 0

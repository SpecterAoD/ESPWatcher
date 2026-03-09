#pragma once

#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#warning "Using config.example.h. Create include/config.local.h for real deployment."
#endif

#ifndef OTA_HOST
#define OTA_HOST NETDATA_HOST
#endif

#ifndef OTA_PORT
#define OTA_PORT NETDATA_PORT
#endif

#ifndef OTA_MANIFEST_PATH
#define OTA_MANIFEST_PATH "/manifest.json"
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

#ifndef DNS_SERVER_IP
#define DNS_SERVER_IP NETDATA_HOST
#endif

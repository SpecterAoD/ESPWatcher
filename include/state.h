#pragma once

#include <Arduino.h>

#include "config.h"

enum class HealthState : uint8_t {
  GREEN = 0,
  YELLOW = 1,
  RED = 2
};

struct MetricPoint {
  float cpuUsage = NAN;
  float ramUsage = NAN;
  float temperature = NAN;
  float load1 = NAN;
  bool throttledAvailable = false;
  bool throttled = false;
  bool netdataReachable = false;
  uint16_t warningAlerts = 0;
  uint16_t criticalAlerts = 0;
  String alertSummary;
  String unavailableMetrics;
  unsigned long timestampMs = 0;
};

// ---- Status-JSON vom piholepi -------------------------------------------------
enum class SvcState : uint8_t {
  UNKNOWN = 0,
  ACTIVE = 1,
  INACTIVE = 2,
  FAILED = 3,
  MISSING = 4  // Dienst auf dem Pi nicht installiert
};

constexpr uint8_t MAX_SERVICES = 12;

struct ServiceEntry {
  char name[24] = {0};
  SvcState state = SvcState::UNKNOWN;
  bool critical = false;
};

// Tri-State fuer Pruefergebnisse: -1 unbekannt, 0 fehlgeschlagen, 1 ok
struct StatusPoint {
  bool reachable = false;
  bool stale = false;
  unsigned long timestampMs = 0;

  String activeDns = "unknown";  // "piholepi", "pizero" oder "unknown"
  int8_t dnsLocal = -1;
  int8_t dnsVip = -1;
  int8_t dnsPizero = -1;

  ServiceEntry services[MAX_SERVICES];
  uint8_t serviceCount = 0;

  float cpuTemp = NAN;
  float load1 = NAN;
  float memUsedPct = NAN;
  int8_t throttled = -1;

  int8_t diskMounted = -1;
  float diskUsedPct = NAN;
  float diskFreeGb = NAN;
  float backupAgeH = NAN;
};

constexpr uint8_t HISTORY_SIZE = 48;

struct RingBuffer {
  float values[HISTORY_SIZE] = {0};
  uint8_t head = 0;
  uint8_t count = 0;
};

struct AppState {
  MetricPoint latest;
  StatusPoint status;
  RingBuffer cpuHistory;
  RingBuffer ramHistory;
  RingBuffer tempHistory;

  HealthState systemHealth = HealthState::YELLOW;
  String healthReason = "starte...";
  bool wifiConnected = false;
  bool otaAutoEnabled = OTA_AUTO_UPDATE_DEFAULT;

  String otaStatus = "idle";
  String localFwVersion;
  String remoteFwVersion;
  String otaLastError;
  String otaFirmwareUrl;
  bool otaUpdateAvailable = false;

  String currentTime = "unavailable";
  String dnsServer = DNS_SERVER_IP;
  String deviceIp = "0.0.0.0";

  unsigned long lastPollMs = 0;
  unsigned long lastStatusPollMs = 0;
  unsigned long lastOtaCheckMs = 0;
  unsigned long lastTimeSyncMs = 0;
};

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

// Sicherungs-/Wartungsauftraege (systemd-Timer auf dem Pi)
enum class JobState : uint8_t { UNKNOWN = 0, OK = 1, LATE = 2, FAIL = 3, RUN = 4, OFF = 5, MISSING = 6 };

constexpr uint8_t MAX_JOBS = 8;
constexpr uint8_t MAX_MESH = 6;

struct JobEntry {
  char name[24] = {0};
  JobState state = JobState::UNKNOWN;
  float ageH = NAN;  // Stunden seit dem letzten Lauf
};

struct MeshEntry {
  char name[20] = {0};
  bool connected = false;
};

// Ampelstufe aus der Diagnose: 0 unbekannt, 1 gruen, 2 gelb, 3 rot
struct DiagInfo {
  bool present = false;   // Diagnose-Datei auf dem Pi vorhanden und lesbar
  char err[8] = {0};      // "perm" / "read", sonst leer
  uint16_t ageS = 0;
  uint8_t netLevel = 0;
  uint8_t devLevel = 0;
  String netText;
  String devText;
  String offline;         // Namen der nicht erreichbaren wichtigen Geraete
  String weak;            // Geraete mit Warnung (z. B. schwaches WLAN-Signal)
  int16_t devOnline = -1;
  int16_t devTotal = -1;
  String wanDsl;
  String wanOnline;
  uint32_t wanDown = 0;   // Rohwerte vom Router (Einheit laut Router)
  uint32_t wanUp = 0;
  MeshEntry mesh[MAX_MESH];
  uint8_t meshCount = 0;
  int16_t failovers24h = -1;
  int16_t failovers7d = -1;
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
  int16_t dnsMsLocal = -1;  // Antwortzeit in ms, -1 = unbekannt
  int16_t dnsMsVip = -1;
  int16_t dnsMsPizero = -1;

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

  JobEntry jobs[MAX_JOBS];
  uint8_t jobCount = 0;
  DiagInfo diag;
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

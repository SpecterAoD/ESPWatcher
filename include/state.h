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
constexpr uint8_t MAX_DEVS = 30;

struct JobEntry {
  char name[24] = {0};
  JobState state = JobState::UNKNOWN;
  float ageH = NAN;  // Stunden seit dem letzten Lauf
};

struct MeshEntry {
  char name[20] = {0};
  bool connected = false;
};

struct DevEntry {
  char name[24] = {0};  // z. B. "infra-mesh-wz", "kalipi"
  uint8_t state = 0;    // 0 unbekannt, 1 gruen, 2 gelb, 3 rot
  bool online = false;
  int16_t ms = -1;      // Ping in ms, -1 = unbekannt
  int16_t rssi = 0;     // WLAN-Signal in dBm, 0 = kein WLAN/unbekannt
};

constexpr uint8_t MAX_LINKS = 10;
constexpr uint8_t MAX_EVENTS = 16;

// Ein Link aus der Linkliste des Pi (wird dort per TCP geprueft).
struct LinkEntry {
  char name[20] = {0};
  char url[64] = {0};
  int16_t ms = -1;  // Verbindungszeit, 0 = nicht erreichbar, -1 = unbekannt
};

// Statuswechsel des ESP (neueste zuletzt im Ringpuffer).
struct EventEntry {
  char when[12] = {0};  // "MM-DD HH:MM" oder "+12 min"
  uint8_t level = 0;    // 1 gruen, 2 gelb, 3 rot
  char text[56] = {0};
};

constexpr uint8_t MAX_HOSTS = 6;
constexpr uint8_t MAX_NAS_DISKS = 8;

// Ein Rechner aus der Mirror-Tabelle (pimirror, piholepi, pizero, kali ...). -1 = unbekannt.
struct HostEntry {
  char name[16] = {0};
  bool ok = false;           // false = keine Antwort (Grund in bad)
  float cpu = -1;            // %
  float mem = -1;            // %
  float disk = -1;           // % belegt
  float freeGb = -1;
  float temp = -1;           // °C
  int32_t uptimeS = -1;
  int16_t updates = -1;
  int16_t security = -1;
  int16_t lastUpdateH = -1;  // Stunden seit letztem Update
  uint8_t flags = 0;         // 1 Neustart noetig, 2 Unterspannung jetzt, 4 gedrosselt, 8 Unterspannung/Drosselung seit Start
  char bad[32] = {0};        // nicht laufende Dienste bzw. Fehlertext
};

struct NasDisk {
  char name[10] = {0};
  int8_t status = -1;        // Synology: 1 = normal
  int16_t temp = -1;
};

// DiskStation per SNMP (ueber den Magic Mirror). Statuscodes: Synology, 1 = normal.
struct NasInfo {
  bool present = false;
  bool online = false;
  char name[16] = {0};
  char err[32] = {0};
  int8_t raid[4] = {-1, -1, -1, -1};
  uint8_t raidCount = 0;
  NasDisk disks[MAX_NAS_DISKS];
  uint8_t diskCount = 0;
  int16_t sysTemp = -1;
  int8_t sys = -1;
  int8_t power = -1;
  int8_t fans[4] = {-1, -1, -1, -1};
  uint8_t fanCount = 0;
  float volUsedPct = -1;
  int32_t volSizeGb = -1;
};

struct HomeInfo {
  bool present = false;      // homedash.json auf dem Pi vorhanden und lesbar
  char err[8] = {0};
  uint16_t ageS = 0;
  HostEntry hosts[MAX_HOSTS];
  uint8_t hostCount = 0;
  NasInfo nas;
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
  DevEntry devs[MAX_DEVS];
  uint8_t devCount = 0;
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
  HomeInfo home;

  LinkEntry links[MAX_LINKS];
  uint8_t linkCount = 0;
  int16_t wanMs = -2;  // Internet-Test vom Pi: ms, -1 = kein Internet, -2 = unbekannt
};

// 120 Werte im 30-s-Takt = 1 Stunde Verlauf (NAN = keine Messung)
constexpr uint8_t HISTORY_SIZE = 120;
constexpr unsigned long HISTORY_STEP_MS = 30000UL;

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
  RingBuffer dnsHistory;   // DNS-Antwortzeit ueber die VIP in ms
  unsigned long lastHistoryMs = 0;

  EventEntry events[MAX_EVENTS];
  uint8_t eventHead = 0;
  uint8_t eventCount = 0;

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

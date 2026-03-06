#pragma once

#include <Arduino.h>

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

constexpr uint8_t HISTORY_SIZE = 48;

struct RingBuffer {
  float values[HISTORY_SIZE] = {0};
  uint8_t head = 0;
  uint8_t count = 0;
};

struct AppState {
  MetricPoint latest;
  RingBuffer cpuHistory;
  RingBuffer ramHistory;
  RingBuffer tempHistory;

  HealthState systemHealth = HealthState::YELLOW;
  bool wifiConnected = false;
  bool otaAutoEnabled = OTA_AUTO_UPDATE_DEFAULT;
  String otaStatus = "idle";
  unsigned long lastPollMs = 0;
  unsigned long lastOtaCheckMs = 0;
};

#include "health_eval.h"

HealthState evaluateHealth(const MetricPoint& point) {
  if (!point.netdataReachable || point.criticalAlerts > 0) {
    return HealthState::RED;
  }

  if (point.warningAlerts > 0 || (point.temperature > 70.0f && !isnan(point.temperature))) {
    return HealthState::YELLOW;
  }

  return HealthState::GREEN;
}

const char* healthToText(HealthState state) {
  switch (state) {
    case HealthState::GREEN:
      return "GREEN";
    case HealthState::YELLOW:
      return "YELLOW";
    case HealthState::RED:
      return "RED";
    default:
      return "UNKNOWN";
  }
}

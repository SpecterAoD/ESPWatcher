#include "telemetry.h"

void telemetryPush(RingBuffer& buffer, float value) {
  if (isnan(value)) {
    return;
  }

  buffer.values[buffer.head] = value;
  buffer.head = (buffer.head + 1) % HISTORY_SIZE;
  if (buffer.count < HISTORY_SIZE) {
    buffer.count++;
  }
}

void telemetryUpdate(AppState& state, const MetricPoint& point) {
  state.latest = point;
  telemetryPush(state.cpuHistory, point.cpuUsage);
  telemetryPush(state.ramHistory, point.ramUsage);
  telemetryPush(state.tempHistory, point.temperature);
}

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
}

void telemetrySample(AppState& state) {
  const StatusPoint& st = state.status;
  float cpu = state.latest.cpuUsage;
  float ram = state.latest.ramUsage;
  float temp = state.latest.temperature;

  // Ohne Netdata: Werte vom Status-Dienst bzw. aus der Mirror-Tabelle nehmen.
  if (isnan(ram) && !isnan(st.memUsedPct)) ram = st.memUsedPct;
  if (isnan(temp) && !isnan(st.cpuTemp)) temp = st.cpuTemp;
  if (isnan(cpu) && st.home.present) {
    for (uint8_t i = 0; i < st.home.hostCount; ++i) {
      const HostEntry& h = st.home.hosts[i];
      if (h.ok && h.cpu >= 0 && strcmp(h.name, "piholepi") == 0) cpu = h.cpu;
    }
  }

  telemetryPush(state.cpuHistory, cpu);
  telemetryPush(state.ramHistory, ram);
  telemetryPush(state.tempHistory, temp);
  if (st.reachable && st.dnsMsVip >= 0) {
    telemetryPush(state.dnsHistory, static_cast<float>(st.dnsMsVip));
  }
}

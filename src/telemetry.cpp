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

void telemetryEvent(AppState& state, uint8_t level, const String& text) {
  EventEntry& e = state.events[state.eventHead];
  state.eventHead = (state.eventHead + 1) % MAX_EVENTS;
  if (state.eventCount < MAX_EVENTS) state.eventCount++;

  // currentTime ist "YYYY-MM-DD HH:MM:SS"; ohne NTP stattdessen die Laufzeit.
  if (state.currentTime.length() >= 16 && state.currentTime[4] == '-') {
    snprintf(e.when, sizeof(e.when), "%s", state.currentTime.substring(5, 16).c_str());
  } else {
    snprintf(e.when, sizeof(e.when), "+%lu min", millis() / 60000UL);
  }
  e.level = level;
  strlcpy(e.text, text.c_str(), sizeof(e.text));
}

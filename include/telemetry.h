#pragma once

#include "state.h"

void telemetryPush(RingBuffer& buffer, float value);
void telemetryUpdate(AppState& state, const MetricPoint& point);

// Schreibt alle HISTORY_STEP_MS einen Messpunkt in die Verlaufspuffer.
void telemetrySample(AppState& state);

// Haengt einen Statuswechsel an das Ereignisprotokoll an (level: 1 gruen, 2 gelb, 3 rot).
void telemetryEvent(AppState& state, uint8_t level, const String& text);

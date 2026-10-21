#pragma once

#include "state.h"

void telemetryPush(RingBuffer& buffer, float value);
void telemetryUpdate(AppState& state, const MetricPoint& point);

// Schreibt alle HISTORY_STEP_MS einen Messpunkt in die Verlaufspuffer.
void telemetrySample(AppState& state);

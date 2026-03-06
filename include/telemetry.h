#pragma once

#include "state.h"

void telemetryPush(RingBuffer& buffer, float value);
void telemetryUpdate(AppState& state, const MetricPoint& point);

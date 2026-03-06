#pragma once

#include "state.h"

HealthState evaluateHealth(const MetricPoint& point);
const char* healthToText(HealthState state);

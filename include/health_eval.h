#pragma once

#include "state.h"

// Bewertet Netdata- und Status-JSON-Daten gemeinsam.
// reason (optional) erhaelt eine kurze deutsche Begruendung, z. B.
// "unbound: Fehler; DNS aktiv auf pizero".
HealthState evaluateHealth(const MetricPoint& point, const StatusPoint& status, String* reason = nullptr);
const char* healthToText(HealthState state);
const char* healthToGerman(HealthState state);

#pragma once

#include "state.h"

// Holt http://STATUS_HOST:STATUS_PORT/STATUS_PATH und fuellt outPoint.
// Rueckgabe: true bei gueltiger Antwort.
bool statusPoll(StatusPoint& outPoint);

const char* svcStateToText(SvcState state);

#pragma once

#include "state.h"

void otaSetup(AppState& state);
void otaLoop(AppState& state);
void otaSetAuto(AppState& state, bool enabled);
bool otaAutoEnabled(const AppState& state);

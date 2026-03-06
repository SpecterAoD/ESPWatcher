#pragma once
#include "state.h"

void mqttInit(State& s);
void mqttLoop(State& s);

// für webui actions
bool mqttPublish(const char* topic, const char* payload, bool retain);

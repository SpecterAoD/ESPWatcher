#pragma once
#include <Arduino.h>
#include "state.h"

// Init OTA HTTP state
void otaHttpInit(State& s);

// Trigger update: version=="" => latest, sonst Rollback-Version "2.0"
void otaHttpRequestUpdate(State& s, const String& version);

// Loop: manifest check + non-blocking download/flash + MD5 verify
void otaHttpLoop(State& s);

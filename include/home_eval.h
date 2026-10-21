#pragma once

#include <Arduino.h>

#include "state.h"

// Bewertung der Hosts- und DiskStation-Werte (gleiche Schwellen wie im Magic Mirror).
// Rueckgabe: 1 gruen, 2 gelb, 3 rot. In note steht eine kurze Begruendung (leer bei gruen).
uint8_t hostLevel(const HostEntry& h, String* note);
uint8_t nasLevel(const NasInfo& n, String* note);

// Synology-Statuscodes als Text (RAID, Platte, System, Netzteil, Luefter).
const char* raidText(int8_t code);

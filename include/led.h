#pragma once

#include "state.h"

// LED-Ampel: gruen = ok, gelb = Achtung, rot blinkt = Stoerung.
// Gelb blinkt schnell, solange kein WLAN besteht.
void ledSetup();
void ledLoop(HealthState health, bool wifiConnected);

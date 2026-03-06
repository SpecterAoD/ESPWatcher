#include "reset_pi.h"
#include "config.h"

void resetPiInit() {
  pinMode(PIN_RESET_PI, OUTPUT);
  digitalWrite(PIN_RESET_PI, LOW); // LOW = Transistor aus
}

void resetPiPulse(uint16_t ms) {
  // Wir ziehen RUN über NPN nach GND => Transistor EIN
  digitalWrite(PIN_RESET_PI, HIGH);
  delay(ms);
  digitalWrite(PIN_RESET_PI, LOW);
}

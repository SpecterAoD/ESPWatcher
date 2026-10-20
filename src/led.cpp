#include "led.h"

#include "config.h"

#if LED_ENABLED

namespace {
void setLeds(bool green, bool yellow, bool red) {
  digitalWrite(LED_GREEN_PIN, green ? HIGH : LOW);
  digitalWrite(LED_YELLOW_PIN, yellow ? HIGH : LOW);
  digitalWrite(LED_RED_PIN, red ? HIGH : LOW);
}
}  // namespace

void ledSetup() {
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);

  // Kurzer Selbsttest: gruen -> gelb -> rot
  setLeds(true, false, false);
  delay(200);
  setLeds(false, true, false);
  delay(200);
  setLeds(false, false, true);
  delay(200);
  setLeds(false, false, false);
}

void ledLoop(HealthState health, bool wifiConnected) {
  if (!wifiConnected) {
    const bool on = (millis() / 250) % 2 == 0;
    setLeds(false, on, false);
    return;
  }

  switch (health) {
    case HealthState::GREEN:
      setLeds(true, false, false);
      break;
    case HealthState::YELLOW:
      setLeds(false, true, false);
      break;
    case HealthState::RED:
    default: {
      const bool on = (millis() / 500) % 2 == 0;
      setLeds(false, false, on);
      break;
    }
  }
}

#else

void ledSetup() {}
void ledLoop(HealthState, bool) {}

#endif

#include "adc_mgr.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

static Adafruit_ADS1115 ads;

static float dividerFactor() {
  return (DIV_RTOP_OHM + DIV_RBOT_OHM) / DIV_RBOT_OHM; // z.B. 2.0 bei 100k/100k
}

// ADS1115: Gain=1 => ±4.096V (LSB 125uV)
// aber Eingang muss unter VDD bleiben -> durch Teiler ok.
static const adsGain_t GAIN = GAIN_ONE;

static uint32_t lastMs = 0;

void adcInit(State& s) {
  if (!ENABLE_ADS1115) return;

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);

  s.adsOk = ads.begin(ADS1115_ADDR);
  if (s.adsOk) {
    ads.setGain(GAIN);
  }
}

static float readChannelVolts(uint8_t ch) {
  int16_t raw = 0;
  switch (ch) {
    case 0: raw = ads.readADC_SingleEnded(0); break;
    case 1: raw = ads.readADC_SingleEnded(1); break;
    case 2: raw = ads.readADC_SingleEnded(2); break;
    case 3: raw = ads.readADC_SingleEnded(3); break;
    default: raw = 0; break;
  }

  // Gain ONE => 4.096V full scale
  float v_adc = (float)raw * (4.096f / 32768.0f);
  float v_in  = v_adc * dividerFactor();
  return v_in;
}

void adcLoop(State& s) {
  if (!ENABLE_ADS1115) return;
  if (!s.adsOk) return;

  if (millis() - lastMs < 1000) return; // 1 Hz
  lastMs = millis();

  // Mehrfach lesen + Mittelwert (ruhiger)
  float sumPi = 0, sumHub = 0;
  for (int i=0;i<4;i++) {
    sumPi  += readChannelVolts(ADS_CH_5V_PI);
    sumHub += readChannelVolts(ADS_CH_5V_HUB);
    delay(5);
  }
  s.v5_pi  = sumPi / 4.0f;
  s.v5_hub = sumHub / 4.0f;
  s.v5_lastMs = millis();

  // sparklines füttern
  s.sp_v5pi.push(s.v5_pi);
  s.sp_v5hub.push(s.v5_hub);
}

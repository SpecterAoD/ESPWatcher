RescueMonitor (ESP8266 D1 mini) – Pi Watchdog + esp-link Liveness + RUN Reset

1) Libraries (Arduino IDE Library Manager)
   - PubSubClient
   - ArduinoOTA (ist meist bereits in ESP8266 Core enthalten)

2) Board settings (Arduino IDE)
   - Board: "LOLIN(WEMOS) D1 R2 & mini"
   - Flash Size: default
   - Upload Speed: 460800 oder 921600 (wenn zickig -> 115200)

3) Konfiguration
   - Datei: config.h
     * WIFI_SSID / WIFI_PASS
     * PI_IP
     * ESPLINK_IP / ESPLINK_SSID
     * AP_SSID / AP_PASS

4) RUN-Reset-Hardware (Pi3)
   - NPN Transistor (BC547/2N3904) als Open Collector:
     D5(GPIO14) -> 10k -> Basis
     Basis -> 100k -> GND (optional empfohlen)
     Emitter -> GND
     Collector -> RUN
   - GND vom ESP mit Pi-GND verbinden.

Hinweis: RUN ist RESET, kein Power-Off.

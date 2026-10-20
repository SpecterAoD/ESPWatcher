# ESPWatcher (ESP8266 / Wemos D1 mini)

Heimnetz-Wächter für den Raspberry Pi **piholepi** (Pi-hole, Unbound, dnsdist, keepalived).
Zeigt auf einer Weboberfläche und einer LED-Ampel auf einen Blick, ob alles läuft, welcher DNS
gerade aktiv ist und welcher Dienst ausgefallen ist. Kein MQTT nötig.

## Datenquellen

1. **Status-JSON** vom piholepi (`pi/espwatcher-status.py`): Dienste, aktiver DNS (piholepi/pizero),
   DNS-Antworten, Datenplatte, Backup-Alter, Temperatur, Unterspannung. Installation: siehe `pi/README.md`.
2. **Netdata** (optional, ergänzend): CPU, RAM, Load, Alarme. Fehlende Werte (z. B. Temperatur)
   werden aus dem Status-JSON ergänzt.

Die Firmware führt selbst keine Pings, Port- oder DNS-Tests aus – das erledigt der Status-Dienst auf dem Pi.

## Bewertung (Ampel)

| Farbe | Bedeutung |
| ----- | --------- |
| grün | alles in Ordnung |
| gelb | Achtung: z. B. nicht kritischer Dienst aus, DNS läuft auf pizero (Failover), Backup > 36 h alt, Platte > 90 % voll, Netdata-Warnung, CPU > 70 °C, Status-JSON oder Netdata nicht erreichbar |
| rot (blinkt) | Störung: kritischer Dienst (pihole-FTL, unbound, dnsdist, keepalived) aus, DNS antwortet nicht, kritischer Netdata-Alarm, CPU > 80 °C, piholepi nicht erreichbar |

Ohne WLAN blinkt die gelbe LED schnell.

## Weboberfläche

- `/` Übersicht mit Gesamtstatus, aktivem DNS, Dienst-Kacheln, DNS-Erreichbarkeit und Messwerten (aktualisiert sich alle 10 s)
- `/charts` Verlauf der letzten Minuten (CPU, RAM, Temperatur)
- `/links` Schnellzugriff auf Netdata, Pi-hole, dnsdist, Status-JSON
- `/api/state` Zustand als JSON

## LED-Ampel anschließen (Wemos D1 mini)

| LED | Pin | GPIO |
| --- | --- | ---- |
| grün | D5 | 14 |
| gelb | D6 | 12 |
| rot | D7 | 13 |

Jede LED mit 220–330 Ω Vorwiderstand gegen GND. Anderes Layout oder ohne LEDs: siehe `LED_*` in der Konfiguration.

## Konfiguration

`include/config.example.h` nach `include/config.local.h` kopieren und mindestens setzen:

- `WIFI_SSID`, `WIFI_PASS`
- `NETDATA_HOST` (piholepi, `192.168.2.6`)

Alle weiteren Werte sind optional (Standardwerte in `include/config.h`), u. a. `PIZERO_HOST`, `DNS_VIP`,
`STATUS_PORT`, `NETDATA_TEMP_CHART`, `SYSLOG_HOST`, `TIME_SERVER_HOST`, `LED_*`.
`config.local.h` wird von git ignoriert.

OTA ist nur aktiv, wenn `OTA_HOST` gesetzt ist (Manifest siehe unten). Der GitHub-Build setzt es automatisch
(`192.168.2.6:8091`, überschreibbar über die Repository-Variablen `OTA_HOST` und `OTA_PORT`) und liefert im Artifact
`firmware` neben der `firmware.bin` auch das passende `manifest.json`. Den Server dafür (nginx auf dem piholepi) und
das Ausrollen beschreibt `pi/ota/README.md`.

## Build und Flashen

```bash
pio run                  # Firmware bauen
pio run -t upload        # per USB auf den D1 mini flashen
pio device monitor       # serielle Ausgabe (115200)
```

Firmware: `.pio/build/d1_mini/firmware.bin`. Meldungen gehen zusätzlich per Syslog an `SYSLOG_HOST`
(Zustandswechsel mit Begründung unter dem Tag `HEALTH`).

## Netdata-Endpunkte

- `/api/v1/data?chart=system.cpu&points=1`
- `/api/v1/data?chart=system.ram&points=1`
- `/api/v1/data?chart=<NETDATA_TEMP_CHART>&points=1` (Standard `sensors.temp`; Chart-Namen prüfen:
  `curl -s http://192.168.2.6:19999/api/v1/charts | grep -io '"sensors[^"]*"'`)
- `/api/v1/data?chart=system.load&points=1`
- `/api/v1/alarms`

## OTA-Manifest

Erwartet unter `http://OTA_HOST:OTA_PORT + OTA_MANIFEST_PATH`.

```json
{
  "version": "3.5.0",
  "url": "/firmware.bin",
  "md5": "0123456789abcdef0123456789abcdef"
}
```

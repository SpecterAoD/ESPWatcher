# ESPWatcher (ESP8266 / Wemos D1 mini)

Netdata-only monitoring firmware for a Raspberry Pi.

## Scope

This firmware intentionally **does not** perform direct checks (ping, port, DNS, service probing, remote commands).
All telemetry is pulled from Netdata API endpoints.

## Required local config

Copy `include/config.example.h` to `include/config.local.h` and set:

- `WIFI_SSID`
- `WIFI_PASS`
- `NETDATA_HOST`
- optional overrides: `OTA_HOST`, `OTA_PORT`, `OTA_MANIFEST_PATH`, `SYSLOG_HOST`, `SYSLOG_PORT`, `TIME_SERVER_HOST`, `DNS_SERVER_IP`

`config.local.h` is ignored by git.

## Build

```bash
pio run
```

Firmware binary output:

```text
.pio/build/d1_mini/firmware.bin
```

## Netdata endpoints used

- `/api/v1/data?chart=system.cpu&points=1`
- `/api/v1/data?chart=system.ram&points=1`
- `/api/v1/data?chart=sensors.temp&points=1`
- `/api/v1/data?chart=system.load&points=1`
- `/api/v1/alarms`
- `/api/v1/data?chart=raspberry_pi.throttled&points=1` (optional)

## OTA manifest format

Expected at `http://OTA_HOST:OTA_PORT + OTA_MANIFEST_PATH`.

```json
{
  "version": "3.4.6",
  "url": "/firmware.bin",
  "md5": "0123456789abcdef0123456789abcdef"
}
```

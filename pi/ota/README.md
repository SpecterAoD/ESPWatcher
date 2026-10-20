# OTA auf dem piholepi – vollautomatisch

Ziel: Nach einem grünen Build auf `main` kommt die neue Firmware von selbst auf den ESP, ohne dass GitHub
je in dein Heimnetz muss.

```
Merge auf main ─► GitHub-Build (grün) ─► Release firmware-<v>.bin + .sha256
                                              │  (alle 10 Min., Pi ruft GitHub ab)
                                              ▼
                    piholepi: prüfen ─► /mnt/data/ota/firmwares ─► Manifest
                                              │  (alle 10 Min., ESP ruft Pi ab)
                                              ▼
                              ESP8266 lädt, prüft MD5, startet neu
```

Es gibt keine Zugangsschlüssel und keine offene Tür: Beide Seiten fragen nur aktiv nach.
Von „Merge" bis „ESP läuft mit neuer Version" vergehen höchstens etwa 25 Minuten.

## Einmalig einrichten (auf dem piholepi)

```bash
git clone --depth 1 https://github.com/SpecterAoD/ESPWatcher.git ~/espwatcher-src
cd ~/espwatcher-src/pi/ota
sudo ./ota-tool.sh install
```

`install` sichert `/mnt/data/ota` nach `/mnt/data/ota.bak-<Zeitstempel>`, setzt das neue `gen_manifest.py` ein, legt den
Abholdienst `espwatcher-ota-pull` (Dienst + Timer) an, schaltet den Timer ein und sucht sofort nach einem Release.

Die vorhandenen Teile (nginx auf 8091, `esp-ota-manifest.*`, Port 8090) bleiben unverändert.

## Was der Abholdienst tut

Alle 10 Minuten fragt er die öffentliche GitHub-Schnittstelle nach dem neuesten Release. Nur wenn dessen Version
**höher** ist als die auf dem Pi, lädt er `firmware-<v>.bin` und `.sha256` und prüft:

- SHA-256 stimmt, Größe 50 KB bis 1 MB, Kennbyte `0xE9` (ESP8266-Image), die Versionsnummer steht in der Datei
- Download nur von `github.com`/`githubusercontent.com` über HTTPS, auch bei Weiterleitungen

Dann legt er die Datei atomar ab, aktualisiert `firmware.bin`, erzeugt das Manifest, **löscht alle älteren Stände**
und erzeugt das Manifest erneut. Fehler führen nie zu einem halben Zustand: Bei einer fehlgeschlagenen Prüfung bleibt alles wie es war.
Ein Downgrade gibt es nicht; ältere Versionen holt man bei Bedarf manuell mit `publish`.

Der Dienst läuft als Root (die Dateien im Ordner gehören teils Root), aber stark eingeschränkt: Er darf nur
`/mnt/data/ota` schreiben und nur TCP/IP sprechen (siehe `espwatcher-ota-pull.service`).

## Befehle

| Befehl | Wirkung |
| ------ | ------- |
| `sudo ./ota-tool.sh status` | Timer, letzte Läufe, Dateien und Manifest-Version |
| `sudo ./ota-tool.sh pull` | sofort nach einem neuen Release sehen |
| `sudo ./ota-tool.sh auto-off` / `auto-on` | Automatik aus-/einschalten |
| `sudo ./ota-tool.sh publish firmware-3.5.0.bin` | Firmware von Hand bereitstellen |
| `sudo ./ota-tool.sh check` | Erreichbarkeit von Manifest und Firmware prüfen |
| `sudo ./ota-tool.sh rollback` | Sicherung zurückspielen, Automatik aus |

Logs: `journalctl -u espwatcher-ota-pull.service`

## Beim ESP

Der GitHub-Build schaltet Auto-OTA standardmäßig **ein** (Repository-Variable `OTA_AUTO`, `0` zum Abschalten). Der ESP
prüft alle 10 Minuten das Manifest und installiert nur, wenn die Version höher ist als die laufende. Auf `http://192.168.2.17`
steht unter „Update", was er gerade tut.

## Erster Flash (einmalig, per USB)

Die jetzt laufende Firmware kennt diese Automatik nicht, deshalb einmal per USB flashen. Die Firmware aus GitHub
enthält **kein WLAN-Passwort** (das Repo ist öffentlich). Der ESP behält die Zugangsdaten, die die alte Firmware im Flash
gespeichert hat. Dafür die Datei nur schreiben, den Flash nicht löschen:

```bash
python3 -m esptool --port /dev/ttyUSB0 --baud 460800 write_flash 0x0 firmware-3.5.0.bin
```

(`firmware-3.5.0.bin` aus dem Artifact des Branch-Builds bzw. nach dem Merge aus dem Release.)

Verbindet sich der ESP danach nicht mit dem WLAN, einmal lokal mit Zugangsdaten bauen:
`include/config.example.h` nach `include/config.local.h` kopieren, `WIFI_SSID`/`WIFI_PASS` eintragen, dann `pio run -t upload`.
Der ESP speichert sie im Flash, und alle späteren OTA-Updates nutzen sie weiter.

## Neue Version veröffentlichen

1. Änderungen machen und `FW_VERSION` in `include/version.h` **erhöhen** (sonst gibt es kein neues Release).
2. Pull Request nach `main` mergen.
3. Fertig. Der Build erzeugt das Release, der Pi holt es, der ESP aktualisiert sich.

Ist die Version schon als Release vorhanden, erzeugt der Build kein neues und zeigt einen Hinweis im Lauf.

## Was gen_manifest.py schreibt

Das bisherige Format bleibt vollständig erhalten (z. B. für Werkzeuge, die `versions` aus dem Manifest lesen);
neu sind nur `version` und `url`.

| Datei | Inhalt |
| ----- | ------ |
| `manifest.json` | `project`, `latest`, `bin`, `size`, `md5`, `versions[]` wie bisher, zusätzlich `version` und `url` |
| `versions.json` | `{"project", "versions": [...]}` wie bisher |
| `version.txt` | höchste Version |
| `firmware-<v>.json` | Einzelinfo je Version |

`/mnt/data/ota/manifest.json` und `version.txt` sind Verknüpfungen in diesen Ordner und daher immer aktuell.

## Tests

`python3 -I test_pull.py` prüft den Abholdienst gegen eine simulierte GitHub-Schnittstelle (16 Fälle: Normalfall, falsche
Prüfsumme, fremde Datei, fremder Host, alte Version, Aufräumen). Der GitHub-Build führt den Test bei jedem Lauf mit aus.

## Hinweise zur Sicherheit

- Der ESP lädt per einfachem HTTP und prüft nur die MD5-Summe. Das schützt vor kaputten Downloads, nicht vor
  Manipulation im LAN. Der Pi prüft dafür vorher SHA-256 und die Herkunft über HTTPS.
- Auto-OTA installiert jede höhere Version, die auf `main` landet. „Build grün" heißt „kompiliert", nicht „funktioniert".
  Das Tor ist der Merge auf `main`: erst mergen, wenn die Version getestet ist. Ein ESP mit kaputter Firmware muss per USB repariert werden.
- Der Python-Server auf Port 8090 (`esp-ota-http.service`) lauscht auf `0.0.0.0`, also auch über Tailscale. Wird er nicht
  mehr gebraucht: `sudo systemctl disable --now esp-ota-http.service`.

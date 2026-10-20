# OTA auf dem piholepi

Der vorhandene Aufbau bleibt: nginx (Port 8091) liefert `/mnt/data/ota/firmwares/` aus, und
`esp-ota-manifest.path` startet `gen_manifest.py`, sobald sich dort etwas ändert.
Neu ist nur ein überarbeitetes `gen_manifest.py` am selben Pfad und ein Werkzeug zum Veröffentlichen.
Geändert werden ausschließlich Dateien unter `/mnt/data/ota`; nginx und systemd bleiben unberührt.

## Einmalig einrichten

```bash
git clone --depth 1 -b claude/status-led-3-5-0 https://github.com/SpecterAoD/ESPWatcher.git ~/espwatcher-src
cd ~/espwatcher-src/pi/ota
sudo ./ota-tool.sh install
```

`install` sichert `/mnt/data/ota` nach `/mnt/data/ota.bak-<Zeitstempel>`, setzt das neue `gen_manifest.py` ein,
erzeugt das Manifest und prüft, ob Manifest und Firmware über `http://192.168.2.6:8091` erreichbar sind.
Rückgängig machen: `sudo ./ota-tool.sh rollback`.

## Neue Firmware veröffentlichen

1. Auf GitHub beim Build das Artifact `firmware` herunterladen und entpacken (enthält `firmware-<Version>.bin`).
2. Auf den Pi kopieren und veröffentlichen:

   ```bash
   sudo ./ota-tool.sh publish firmware-3.5.0.bin
   ```

3. Auf der Seite des ESP (`http://192.168.2.17`) Auto-OTA einschalten. Der ESP prüft alle 10 Minuten und
   installiert nur, wenn die Version im Manifest höher ist als die laufende. Danach Auto-OTA wieder ausschalten.

`publish` kopiert zuerst unter einem versteckten Namen und benennt dann um, damit der Manifest-Dienst die Datei
nie halb geschrieben sieht.

## Was `gen_manifest.py` schreibt

Das bisherige Ausgabeformat bleibt vollständig erhalten, damit bestehende Werkzeuge (z. B. eine Versionsauswahl, die
`versions` aus dem Manifest liest) weiter funktionieren. Neu sind nur die Felder `version` und `url`.

| Datei | Inhalt |
| ----- | ------ |
| `manifest.json` | `project`, `latest`, `bin`, `size`, `md5`, `versions[]` wie bisher, zusätzlich `version` und `url` |
| `versions.json` | `{"project", "versions": [...]}` wie bisher |
| `version.txt` | höchste Version |
| `firmware-<v>.json` | Einzelinfo je Version |

`firmware.bin` und die Kopien in `/mnt/data/ota` (Port 8090) fasst das Skript wie das alte **nicht** an. Wer sie braucht,
pflegt sie weiter von Hand.

## Hinweis zur Sicherheit

Der ESP lädt per einfachem HTTP und prüft nur die MD5-Summe. Das schützt vor kaputten Downloads, nicht vor Manipulation.
Der Python-Server auf Port 8090 (`esp-ota-http.service`) lauscht auf `0.0.0.0`, also auch über Tailscale.
Wird er nicht mehr gebraucht, kann man ihn abschalten (`sudo systemctl disable --now esp-ota-http.service`).

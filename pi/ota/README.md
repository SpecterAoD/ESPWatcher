# OTA auf dem piholepi

Der vorhandene Aufbau bleibt: nginx (Port 8091) liefert `/mnt/data/ota/firmwares/` aus, und
`esp-ota-manifest.path` startet `gen_manifest.py`, sobald sich dort etwas ändert.
Neu ist nur ein sauberes `gen_manifest.py` am selben Pfad und ein Werkzeug zum Veröffentlichen.
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

| Datei | Inhalt |
| ----- | ------ |
| `manifest.json` | klein (ca. 200 Byte): `version`, `url`, `md5` sowie `latest`, `bin`, `size` |
| `versions.json` | Liste aller Versionen mit Größe und MD5 |
| `version.txt` | höchste Version |
| `firmware.bin` | Kopie der höchsten Version |
| `firmware-<v>.json` | Einzelinfo je Version |

Liegt im Elternordner `/mnt/data/ota` bereits ein `manifest.json` oder `version.txt`, werden diese ebenfalls aktualisiert
(der alte Aufbau lieferte sie dort über Port 8090 aus).

## Hinweis zur Sicherheit

Der ESP lädt per einfachem HTTP und prüft nur die MD5-Summe. Das schützt vor kaputten Downloads, nicht vor Manipulation.
Der Python-Server auf Port 8090 (`esp-ota-http.service`) lauscht auf `0.0.0.0`, also auch über Tailscale.
Wird er nicht mehr gebraucht, kann man ihn abschalten (`sudo systemctl disable --now esp-ota-http.service`).

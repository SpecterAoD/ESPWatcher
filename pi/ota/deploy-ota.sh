#!/usr/bin/env bash
# Legt eine neue Firmware auf dem OTA-Server ab (auf dem piholepi ausfuehren).
#
# Aufruf:  sudo ./deploy-ota.sh <ordner-mit-firmware.bin-und-manifest.json>
# Der Ordner ist der entpackte Artifact "firmware" aus dem GitHub-Build.
#
# Das Skript prueft vor dem Austausch die MD5-Summe und ersetzt die Dateien
# atomar, damit ein ESP nie eine halbe Datei abholt. Die Firmware wird zuerst
# abgelegt, das Manifest zuletzt - erst dann sieht der ESP die neue Version.
set -euo pipefail

SRC="${1:?Aufruf: $0 <ordner-mit-firmware.bin-und-manifest.json>}"
DEST="${OTA_DIR:-/srv/espwatcher-ota}"

[ -f "$SRC/firmware.bin" ]   || { echo "FEHLER: $SRC/firmware.bin fehlt" >&2; exit 1; }
[ -f "$SRC/manifest.json" ]  || { echo "FEHLER: $SRC/manifest.json fehlt" >&2; exit 1; }

want="$(sed -n 's/.*"md5"[[:space:]]*:[[:space:]]*"\([0-9a-f]\{32\}\)".*/\1/p' "$SRC/manifest.json")"
have="$(md5sum "$SRC/firmware.bin" | cut -d' ' -f1)"
if [ -z "$want" ] || [ "$want" != "$have" ]; then
    echo "FEHLER: MD5 passt nicht (Manifest: ${want:-leer}, Datei: $have)" >&2
    exit 1
fi

install -d -m 0755 "$DEST"

install -m 0644 "$SRC/firmware.bin"  "$DEST/.firmware.bin.new"
mv -f "$DEST/.firmware.bin.new" "$DEST/firmware.bin"

install -m 0644 "$SRC/manifest.json" "$DEST/.manifest.json.new"
mv -f "$DEST/.manifest.json.new" "$DEST/manifest.json"

echo "OK: $(sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$DEST/manifest.json") liegt bereit in $DEST"

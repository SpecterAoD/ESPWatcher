#!/usr/bin/env bash
# OTA-Werkzeug fuer den piholepi. Mit Root-Rechten ausfuehren.
#
#   sudo ./ota-tool.sh install     einmalig: alles einrichten (Sicherung, gen_manifest.py,
#                                   automatische Abholung von GitHub per Timer)
#   sudo ./ota-tool.sh pull         sofort nach neuer Firmware auf GitHub sehen (statt auf den Timer zu warten)
#   sudo ./ota-tool.sh status       Timer, letzter Lauf, installierte Version
#   sudo ./ota-tool.sh publish <firmware-<version>.bin>   Firmware von Hand bereitstellen
#   sudo ./ota-tool.sh check        Erreichbarkeit des OTA-Servers pruefen
#   sudo ./ota-tool.sh auto-off     automatische Abholung abschalten (auto-on schaltet sie wieder ein)
#   sudo ./ota-tool.sh rollback     Sicherung zurueckspielen und Automatik abschalten
#
# Aendert nur Dateien unter /mnt/data/ota sowie die beiden Units espwatcher-ota-pull.{service,timer}.
# nginx und die vorhandenen esp-ota-*-Units bleiben unberuehrt.
set -euo pipefail

OTA_ROOT="${OTA_ROOT:-/mnt/data/ota}"
FW_DIR="$OTA_ROOT/firmwares"
BACKUP_MARK="$OTA_ROOT/.last-backup"
URL="${OTA_URL:-http://192.168.2.6:8091}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

die() { echo "FEHLER: $*" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || die "bitte mit sudo ausfuehren"
[ -d "$FW_DIR" ] || die "$FW_DIR existiert nicht"

cmd_install() {
    local f
    for f in gen_manifest.py espwatcher-ota-pull.py espwatcher-ota-pull.service espwatcher-ota-pull.timer; do
        [ -f "$HERE/$f" ] || die "$f liegt nicht neben diesem Skript"
    done
    python3 -I -m py_compile "$HERE/gen_manifest.py" "$HERE/espwatcher-ota-pull.py" || die "Syntaxfehler in den Python-Skripten"
    rm -rf "$HERE/__pycache__"

    local stamp backup
    stamp="$(date +%Y%m%d-%H%M%S)"
    backup="$OTA_ROOT.bak-$stamp"
    cp -a "$OTA_ROOT" "$backup"
    echo "$backup" > "$BACKUP_MARK"
    echo "Sicherung: $backup"

    install -o root -g root -m 0755 "$HERE/gen_manifest.py" "$FW_DIR/gen_manifest.py"
    install -o root -g root -m 0755 "$HERE/espwatcher-ota-pull.py" /usr/local/bin/espwatcher-ota-pull.py
    install -o root -g root -m 0644 "$HERE/espwatcher-ota-pull.service" /etc/systemd/system/espwatcher-ota-pull.service
    install -o root -g root -m 0644 "$HERE/espwatcher-ota-pull.timer" /etc/systemd/system/espwatcher-ota-pull.timer
    systemctl daemon-reload

    python3 -I "$FW_DIR/gen_manifest.py"
    echo "--- manifest.json ---"
    cat "$FW_DIR/manifest.json"
    cmd_check || true

    systemctl enable --now espwatcher-ota-pull.timer
    echo "Automatische Abholung ist eingeschaltet (alle 10 Minuten)."
    cmd_pull
}

cmd_pull() {
    echo "--- Suche nach neuer Firmware auf GitHub ---"
    systemctl start espwatcher-ota-pull.service || true
    journalctl -u espwatcher-ota-pull.service -n 15 --no-pager -o cat
}

cmd_status() {
    systemctl list-timers espwatcher-ota-pull.timer --no-pager || true
    echo "--- letzte Laeufe ---"
    journalctl -u espwatcher-ota-pull.service -n 10 --no-pager -o cat
    echo "--- auf dem Pi ---"
    ls -1 "$FW_DIR" | grep -E '^firmware' || true
    echo "Manifest: $(python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); print(m.get("version") or m.get("latest"))' "$FW_DIR/manifest.json" 2>/dev/null || echo unbekannt)"
}

cmd_auto_on()  { systemctl enable --now espwatcher-ota-pull.timer; echo "Automatik an."; }
cmd_auto_off() { systemctl disable --now espwatcher-ota-pull.timer; echo "Automatik aus."; }

cmd_publish() {
    local src="${1:-}"
    [ -n "$src" ] && [ -f "$src" ] || die "Aufruf: $0 publish <firmware-<version>.bin>"
    local name
    name="$(basename "$src")"
    [[ "$name" =~ ^firmware-[0-9]+(\.[0-9]+)*\.bin$ ]] || die "Dateiname muss firmware-<version>.bin lauten (z. B. firmware-3.5.0.bin)"
    [ -s "$src" ] || die "Datei ist leer"

    # Erst unter verstecktem Namen kopieren, dann umbenennen: der Manifest-Dienst
    # (PathModified) sieht die Datei so nie halb geschrieben.
    install -o root -g root -m 0644 "$src" "$FW_DIR/.$name.part"
    mv -f "$FW_DIR/.$name.part" "$FW_DIR/$name"
    echo "abgelegt: $FW_DIR/$name"

    # Der Dienst zieht das Manifest automatisch nach; zur Sicherheit direkt ausfuehren.
    python3 -I "$FW_DIR/gen_manifest.py"
    cmd_check
}

cmd_check() {
    echo "--- Pruefung ($URL) ---"
    local ok=0
    curl -fsS -m 5 "$URL/manifest.json" | python3 -c '
import json, sys
m = json.load(sys.stdin)
print("Manifest erreichbar: neueste Version", m.get("version") or m.get("latest"), "->", m.get("url") or m.get("bin"))
' || ok=1
    local bin
    bin="$(python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); print((m.get("url") or "/"+m["bin"]).lstrip("/"))' "$FW_DIR/manifest.json")"
    code="$(curl -s -o /dev/null -m 10 -w '%{http_code}' -I "$URL/$bin" || true)"
    echo "Firmware-Datei $bin: HTTP $code"
    [ "$code" = "200" ] || ok=1
    [ "$ok" -eq 0 ] && echo "OK" || { echo "WARNUNG: Pruefung fehlgeschlagen (nginx-Konfiguration pruefen)"; return 1; }
}

cmd_rollback() {
    [ -f "$BACKUP_MARK" ] || die "keine Sicherung vermerkt"
    local backup
    backup="$(cat "$BACKUP_MARK")"
    [ -d "$backup" ] || die "Sicherung $backup nicht gefunden"
    systemctl disable --now espwatcher-ota-pull.timer 2>/dev/null || true
    cp -a "$backup/firmwares/gen_manifest.py" "$FW_DIR/gen_manifest.py"
    for f in manifest.json version.txt versions.json; do
        [ -f "$backup/firmwares/$f" ] && cp -a "$backup/firmwares/$f" "$FW_DIR/$f"
        [ -f "$backup/$f" ] && cp -a "$backup/$f" "$OTA_ROOT/$f"
    done
    echo "Alter Stand aus $backup wiederhergestellt, Automatik ist aus. Die Units bleiben installiert (auto-on schaltet sie wieder ein)."
}

case "${1:-}" in
    install)  cmd_install ;;
    pull)     cmd_pull ;;
    status)   cmd_status ;;
    auto-on)  cmd_auto_on ;;
    auto-off) cmd_auto_off ;;
    publish)  shift; cmd_publish "${1:-}" ;;
    check)    cmd_check ;;
    rollback) cmd_rollback ;;
    *) sed -n '2,14p' "$0"; exit 2 ;;
esac

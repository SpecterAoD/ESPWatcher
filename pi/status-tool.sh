#!/usr/bin/env bash
# Status-Dienst fuer den piholepi. Mit Root-Rechten ausfuehren.
#
#   sudo ./status-tool.sh install     einrichten (Pruefungen, Benutzer, Dienst, Selbsttest)
#   sudo ./status-tool.sh status      Dienst und aktuelle Antwort anzeigen
#   sudo ./status-tool.sh uninstall   Dienst, Skript und Benutzer wieder entfernen
#
# Aendert nur: /usr/local/bin/espwatcher-status.py, /etc/systemd/system/espwatcher-status.service
# den Systembenutzer "espwatcher" und (bei aktivem ufw) eine Regel fuer das LAN.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT=/usr/local/bin/espwatcher-status.py
UNIT=/etc/systemd/system/espwatcher-status.service
SVC=espwatcher-status.service
BIND="${ESPW_BIND:-192.168.2.6}"
PORT="${ESPW_PORT:-8092}"

die() { echo "FEHLER: $*" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || die "bitte mit sudo ausfuehren"

fetch() { python3 -I - "$1" <<'PY'
import sys, urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=5) as r:
    sys.stdout.write(r.read().decode())
PY
}

show() {
    local json
    json="$(fetch "http://$BIND:$PORT/status.json")" || return 1
    echo "$json" | python3 -I -c '
import json, sys
d = json.load(sys.stdin)
print("Aktiver DNS     :", d.get("active"))
dns = d.get("dns", {})
print("DNS antwortet   : lokal=%s vip=%s pizero=%s" % (dns.get("local"), dns.get("vip"), dns.get("pizero")))
for s in d.get("svc", []):
    mark = "OK " if s["s"] == "active" else "!! "
    print("  %s%-20s %-9s%s" % (mark, s["n"], s["s"], " (kritisch)" if s["c"] else ""))
jobs = d.get("jobs", [])
if jobs:
    print("Sicherungen:")
    for j in jobs:
        print("  %-22s %-8s %s" % (j["n"], j["s"], ("vor %s h" % j["a"]) if "a" in j else ""))
diag = d.get("diag")
if diag is None:
    print("Netz-Diagnose   : keine Datei (mirror-diag nicht aktiv?)")
elif "err" in diag:
    print("Netz-Diagnose   : nicht lesbar (%s) - Rechte auf /run/mirror-diag/diag.json pruefen" % diag["err"])
else:
    print("Netz-Diagnose   : netz=%s | geraete=%s | offline=%s | internet=%s" % (diag["net"]["s"], diag["dev"]["s"], diag.get("off"), diag.get("wan")))
print("Platte          :", d.get("disk"))
print("Temp/Load/RAM   :", d.get("temp"), d.get("load1"), d.get("mem"))
print("Unterspannung   :", d.get("thr"))
print("Alter der Daten : %s s" % d.get("age_s"))
'
    echo
    echo "Rohdaten: $json"
}

cmd_install() {
    command -v python3 >/dev/null || die "python3 fehlt"
    command -v systemctl >/dev/null || die "systemd fehlt"
    local f
    for f in espwatcher-status.py espwatcher-status.service; do
        [ -f "$HERE/$f" ] || die "$f liegt nicht neben diesem Skript"
    done
    python3 -I -m py_compile "$HERE/espwatcher-status.py" || die "Syntaxfehler im Skript"
    rm -rf "$HERE/__pycache__"
    ip -4 -o addr show | grep -q " $BIND/" || die "$BIND liegt nicht auf diesem Geraet (ESPW_BIND anpassen)"

    if ! systemctl is-active --quiet "$SVC"; then
        if ss -ltn "( sport = :$PORT )" | grep -q ":$PORT"; then
            die "Port $PORT ist schon belegt (anderer Dienst)"
        fi
    fi

    # Vorherigen Stand merken, damit ein Fehlschlag zurueckgedreht werden kann
    local had_script=0 had_unit=0 tmp
    tmp="$(mktemp -d)"
    [ -f "$SCRIPT" ] && { cp -a "$SCRIPT" "$tmp/script"; had_script=1; }
    [ -f "$UNIT" ] && { cp -a "$UNIT" "$tmp/unit"; had_unit=1; }

    rollback() {
        echo "Selbsttest fehlgeschlagen - stelle den vorherigen Stand wieder her" >&2
        systemctl disable --now "$SVC" 2>/dev/null || true
        if [ "$had_script" = 1 ]; then cp -a "$tmp/script" "$SCRIPT"; else rm -f "$SCRIPT"; fi
        if [ "$had_unit" = 1 ]; then cp -a "$tmp/unit" "$UNIT"; else rm -f "$UNIT"; fi
        systemctl daemon-reload
        [ "$had_unit" = 1 ] && systemctl restart "$SVC" || true
        journalctl -u "$SVC" -n 20 --no-pager >&2 || true
        rm -rf "$tmp"
        exit 1
    }

    if ! id espwatcher >/dev/null 2>&1; then
        local groups=()
        getent group video >/dev/null && groups=(--groups video)
        useradd --system --no-create-home --shell /usr/sbin/nologin "${groups[@]}" espwatcher
        echo "Benutzer espwatcher angelegt"
    fi

    install -m 0755 "$HERE/espwatcher-status.py" "$SCRIPT"
    install -m 0644 "$HERE/espwatcher-status.service" "$UNIT"
    # Linkliste fuer die ESP-Oberflaeche: nur anlegen, eigene Aenderungen bleiben erhalten
    install -d -m 0755 /etc/espwatcher
    if [ ! -e /etc/espwatcher/links.json ]; then
        install -m 0644 "$HERE/links.example.json" /etc/espwatcher/links.json
        echo "Linkliste angelegt: /etc/espwatcher/links.json (dort anpassen)"
    fi
    systemctl daemon-reload
    systemctl enable "$SVC" >/dev/null 2>&1
    systemctl restart "$SVC"

    local i ok=0
    for i in $(seq 1 15); do
        if fetch "http://$BIND:$PORT/status.json" >/dev/null 2>&1; then ok=1; break; fi
        python3 -I -c 'import time; time.sleep(1)'
    done
    [ "$ok" = 1 ] || rollback
    rm -rf "$tmp"
    # ufw: Port nur fuer das LAN freigeben (additiv, nur wenn ufw aktiv ist)
    if command -v ufw >/dev/null && ufw status | grep -q "^Status: active"; then
        if ! ufw status | grep -E "^$PORT/tcp.*ALLOW.*192\.168\.2\.0/24" >/dev/null; then
            ufw allow from 192.168.2.0/24 to any port "$PORT" proto tcp comment 'ESPWatcher Status LAN' >/dev/null
            echo "ufw: Port $PORT fuer 192.168.2.0/24 freigegeben"
        fi
    fi
    echo "Dienst laeuft. Aktuelle Antwort:"
    echo
    show
    echo
    echo "Naechster Schritt: im Browser des PCs http://$BIND:$PORT/status.json oeffnen."
    echo "Kommt dort keine Antwort, blockiert eine Firewall den TCP-Port $PORT aus dem LAN."
}

cmd_status() {
    systemctl --no-pager status "$SVC" | head -12 || true
    echo
    show || echo "Keine Antwort von http://$BIND:$PORT/status.json"
}

cmd_uninstall() {
    systemctl disable --now "$SVC" 2>/dev/null || true
    rm -f "$UNIT" "$SCRIPT"
    systemctl daemon-reload
    id espwatcher >/dev/null 2>&1 && userdel espwatcher || true
    if command -v ufw >/dev/null; then
        ufw delete allow from 192.168.2.0/24 to any port "$PORT" proto tcp >/dev/null 2>&1 || true
    fi
    echo "Status-Dienst entfernt."
}

case "${1:-}" in
    install) cmd_install ;;
    status) cmd_status ;;
    uninstall) cmd_uninstall ;;
    *) sed -n '2,9p' "$0"; exit 2 ;;
esac

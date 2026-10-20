#!/usr/bin/env python3
"""ESPWatcher-Status fuer den piholepi (nur lesend, nur Python-Standardbibliothek).

Sammelt alle 5 Sekunden im Hintergrund den Zustand der wichtigen Dienste und
liefert ihn als kleines JSON unter /status.json aus. Der ESP8266 holt nur diese
eine Datei, statt viele Einzelabfragen zu machen.

Konfiguration ueber Umgebungsvariablen (siehe espwatcher-status.service):
  ESPW_BIND           Adresse zum Lauschen        (Standard 192.168.2.6)
  ESPW_PORT           Port                        (Standard 8092)
  ESPW_VIP            keepalived-Adresse          (Standard 192.168.2.10)
  ESPW_PIZERO         Adresse des Reserve-DNS     (Standard 192.168.2.5)
  ESPW_DNS_LOCAL      DNS-Adresse dieses Pi       (Standard = ESPW_BIND)
  ESPW_DISK_PATH      Datenplatte                 (Standard /mnt/data)
  ESPW_BACKUP_MARKER  Datei, deren Aenderungszeit dem letzten erfolgreichen
                      Backup entspricht (optional, leer = nicht anzeigen)
  ESPW_SERVICES       Dienste als "name:1,name:0" (1 = kritisch), ersetzt die Liste unten
  ESPW_ALLOW          erlaubte Netze, kommagetrennt (Standard 192.168.2.0/24,127.0.0.0/8)
  ESPW_DNS_EVERY      Sekunden zwischen den DNS-Pruefungen (Standard 60; jede Pruefung
                      taucht in der Pi-hole-Statistik auf, deshalb nicht zu haeufig)
"""

import ipaddress
import json
import os
import random
import socket
import struct
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BIND = os.environ.get("ESPW_BIND", "192.168.2.6")
PORT = int(os.environ.get("ESPW_PORT", "8092"))
VIP = os.environ.get("ESPW_VIP", "192.168.2.10")
PIZERO = os.environ.get("ESPW_PIZERO", "192.168.2.5")
DNS_LOCAL = os.environ.get("ESPW_DNS_LOCAL", BIND)
DISK_PATH = os.environ.get("ESPW_DISK_PATH", "/mnt/data")
BACKUP_MARKER = os.environ.get("ESPW_BACKUP_MARKER", "")
DNS_TEST_NAME = os.environ.get("ESPW_DNS_TEST_NAME", "pi-hole.net")
REFRESH_S = 5
DNS_EVERY_S = int(os.environ.get("ESPW_DNS_EVERY", "60"))
DNS_PORT = int(os.environ.get("ESPW_DNS_PORT", "53"))  # nur fuer Tests aendern
ALLOWED_NETS = [
    ipaddress.ip_network(part.strip())
    for part in os.environ.get("ESPW_ALLOW", "192.168.2.0/24,127.0.0.0/8").split(",")
    if part.strip()
]

# (systemd-Dienst, kritisch?) - kritisch = ESP zeigt bei Ausfall ROT, sonst GELB.
DEFAULT_SERVICES = [
    # DNS-Kette: faellt einer aus, ist das Heimnetz betroffen -> ESP zeigt ROT
    ("pihole-FTL", 1),
    ("unbound", 1),
    ("dnsdist", 1),
    ("keepalived", 1),
    # uebrige Dienste: Ausfall ist unschoen, aber nicht akut -> ESP zeigt GELB
    ("nginx", 0),
    ("chrony", 0),
    ("netdata", 0),
    ("tailscaled", 0),
    ("pihole-dns-policy", 0),
    ("nft-localonly", 0),
]


def parse_services():
    raw = os.environ.get("ESPW_SERVICES", "").strip()
    if not raw:
        return DEFAULT_SERVICES
    result = []
    for item in raw.split(","):
        name, _, crit = item.strip().partition(":")
        if name:
            result.append((name, 1 if crit.strip() == "1" else 0))
    return result


SERVICES = parse_services()
_snapshot = {"ts": 0.0, "data": {}}
_lock = threading.Lock()


def service_state(name):
    """Liefert active | inactive | failed | missing | unknown."""
    try:
        out = subprocess.run(
            ["systemctl", "show", "-p", "LoadState", "-p", "ActiveState", name],
            capture_output=True, text=True, timeout=3, check=False,
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    props = dict(line.split("=", 1) for line in out.splitlines() if "=" in line)
    if props.get("LoadState") == "not-found":
        return "missing"
    state = props.get("ActiveState", "unknown")
    return state if state in ("active", "inactive", "failed") else "unknown"


def vip_is_local():
    """True, wenn die keepalived-Adresse gerade auf diesem Pi liegt."""
    try:
        out = subprocess.run(
            ["ip", "-4", "-o", "addr", "show"],
            capture_output=True, text=True, timeout=3, check=False,
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    return f" {VIP}/" in out


def dns_ok(server, timeout=1.5):
    """Einfache A-Abfrage per UDP; True, wenn eine gueltige Antwort (NOERROR) kommt."""
    qid = random.randrange(65536)
    query = struct.pack(">HHHHHH", qid, 0x0100, 1, 0, 0, 0)
    for part in DNS_TEST_NAME.split("."):
        query += bytes([len(part)]) + part.encode()
    query += b"\x00" + struct.pack(">HH", 1, 1)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    try:
        sock.sendto(query, (server, DNS_PORT))
        data, _ = sock.recvfrom(512)
        return len(data) >= 12 and struct.unpack(">H", data[:2])[0] == qid and (data[3] & 0x0F) == 0
    except OSError:
        return False
    finally:
        sock.close()


def read_float(path, scale=1.0):
    try:
        with open(path, "r", encoding="ascii") as fh:
            return float(fh.read().strip()) / scale
    except (OSError, ValueError):
        return None


def mem_used_pct():
    try:
        info = {}
        with open("/proc/meminfo", "r", encoding="ascii") as fh:
            for line in fh:
                key, _, rest = line.partition(":")
                info[key] = int(rest.split()[0])
        return round(100.0 * (1.0 - info["MemAvailable"] / info["MemTotal"]), 1)
    except (OSError, KeyError, ValueError, ZeroDivisionError):
        return None


def throttled():
    """1 bei aktueller Unterspannung/Drosselung, 0 sonst, None wenn nicht ermittelbar."""
    try:
        out = subprocess.run(
            ["vcgencmd", "get_throttled"], capture_output=True, text=True, timeout=3, check=False
        ).stdout
        value = int(out.strip().split("=")[1], 16)
    except (OSError, subprocess.SubprocessError, IndexError, ValueError):
        return None
    return 1 if value & 0xF else 0


def disk_info():
    info = {"mounted": os.path.ismount(DISK_PATH)}
    if info["mounted"]:
        try:
            st = os.statvfs(DISK_PATH)
            total = st.f_blocks * st.f_frsize
            free = st.f_bavail * st.f_frsize
            if total:
                info["used"] = round(100.0 * (1.0 - free / total), 1)
                info["free_gb"] = round(free / 1024 ** 3, 1)
        except OSError:
            pass
    return info


def backup_age_hours():
    if not BACKUP_MARKER:
        return None
    try:
        return round((time.time() - os.path.getmtime(BACKUP_MARKER)) / 3600.0, 1)
    except OSError:
        return None


_dns_cache = {"ts": 0.0, "res": (False, False, False)}


def dns_results():
    """DNS-Antworten lokal / VIP / pizero, hoechstens alle DNS_EVERY_S Sekunden neu."""
    now = time.time()
    if _dns_cache["ts"] == 0.0 or now - _dns_cache["ts"] >= DNS_EVERY_S:
        _dns_cache["res"] = (dns_ok(DNS_LOCAL), dns_ok(VIP), dns_ok(PIZERO))
        _dns_cache["ts"] = now
    return _dns_cache["res"]


def collect():
    vip_local = vip_is_local()
    dns_local, dns_vip, dns_pizero = dns_results()

    if vip_local:
        active = "piholepi"
    elif vip_local is False and dns_vip:
        active = "pizero"
    else:
        active = "unknown"

    data = {
        "v": 1,
        "host": socket.gethostname(),
        "active": active,
        "dns": {"local": int(dns_local), "vip": int(dns_vip), "pizero": int(dns_pizero)},
        "svc": [{"n": n, "s": service_state(n), "c": c} for n, c in SERVICES],
        "disk": disk_info(),
    }
    temp = read_float("/sys/class/thermal/thermal_zone0/temp", 1000.0)
    if temp is not None:
        data["temp"] = round(temp, 1)
    data["load1"] = round(os.getloadavg()[0], 2)
    mem = mem_used_pct()
    if mem is not None:
        data["mem"] = mem
    thr = throttled()
    if thr is not None:
        data["thr"] = thr
    backup = backup_age_hours()
    if backup is not None:
        data["backup_h"] = backup
    return data


def updater():
    while True:
        try:
            data = collect()
            with _lock:
                _snapshot["ts"] = time.time()
                _snapshot["data"] = data
        except Exception as exc:  # noqa: BLE001 - Dienst soll nie wegen eines Fehlers enden
            print(f"collect failed: {exc}", file=sys.stderr, flush=True)
        time.sleep(REFRESH_S)


def client_allowed(address):
    try:
        ip = ipaddress.ip_address(address)
    except ValueError:
        return False
    return any(ip in net for net in ALLOWED_NETS)


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):  # noqa: N802
        if not client_allowed(self.client_address[0]):
            self.send_error(403)
            return
        if self.path.split("?")[0] not in ("/status.json", "/healthz"):
            self.send_error(404)
            return
        with _lock:
            payload = dict(_snapshot["data"])
            ts = _snapshot["ts"]
        if not payload:
            self.send_error(503, "noch keine Daten")
            return
        payload["ts"] = int(ts)
        payload["age_s"] = round(time.time() - ts, 1)
        body = json.dumps(payload, separators=(",", ":")).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):  # Zugriffe nicht ins Journal schreiben
        pass


def main():
    threading.Thread(target=updater, daemon=True).start()
    server = ThreadingHTTPServer((BIND, PORT), Handler)
    print(f"espwatcher-status auf http://{BIND}:{PORT}/status.json", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()

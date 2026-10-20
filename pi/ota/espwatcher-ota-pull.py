#!/usr/bin/env python3
"""Holt neue ESPWatcher-Firmware von GitHub und stellt sie dem ESP bereit (laeuft auf dem piholepi).

Ablauf (vom systemd-Timer alle 10 Minuten gestartet):
  1. Neuestes GitHub-Release des Repos abfragen (oeffentliche API, keine Zugangsdaten).
  2. Nur wenn die Version HOEHER ist als die auf dem Pi liegende: firmware-<v>.bin und
     firmware-<v>.bin.sha256 laden.
  3. Pruefen: SHA-256, Groesse, ESP-Image-Kennung (0xE9), Versionstext in der Datei.
  4. Atomar in den Firmware-Ordner legen, firmware.bin aktualisieren, Manifest erzeugen
     (gen_manifest.py), alle aelteren Staende loeschen, Manifest erneut erzeugen.

Sicherheitsregeln: nur das konfigurierte Repo, nur github.com/githubusercontent.com ueber
HTTPS (auch bei Weiterleitungen), nie ein Downgrade, harte Groessengrenzen, eine Instanz
gleichzeitig. Das Skript schreibt ausschliesslich in den Firmware-Ordner.

Optionen:  --dry-run   nur berichten, nichts veraendern

Umgebungsvariablen (Standardwerte passen fuer den piholepi):
  ESPW_REPO      SpecterAoD/ESPWatcher
  ESPW_FW_DIR    /mnt/data/ota/firmwares
  ESPW_GEN       <ESPW_FW_DIR>/gen_manifest.py
  ESPW_LOCK      /run/espwatcher-ota/pull.lock
  Nur fuer Tests:  ESPW_API_BASE, ESPW_ALLOW_HOSTS (kommagetrennt, erlaubt auch HTTP)
"""

import fcntl
import hashlib
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

REPO = os.environ.get("ESPW_REPO", "SpecterAoD/ESPWatcher")
FW_DIR = os.path.abspath(os.environ.get("ESPW_FW_DIR", "/mnt/data/ota/firmwares"))
GEN = os.environ.get("ESPW_GEN", os.path.join(FW_DIR, "gen_manifest.py"))
LOCK = os.environ.get("ESPW_LOCK", "/run/espwatcher-ota/pull.lock")
API = os.environ.get("ESPW_API_BASE", "https://api.github.com").rstrip("/")
EXTRA_HOSTS = {h for h in os.environ.get("ESPW_ALLOW_HOSTS", "").split(",") if h}

ALLOWED_DOMAINS = ("github.com", "githubusercontent.com")
MIN_SIZE = 50_000
MAX_SIZE = 1_000_000  # ESP8266-OTA-Slot bei 4 MB Flash
TIMEOUT = 20

BIN_RE = re.compile(r"^firmware-(\d+(?:\.\d+)*)\.bin$")
META_RE = re.compile(r"^firmware-(\d+(?:\.\d+)*)\.json$")
PART_RE = re.compile(r"^\.firmware.*\.(?:part|tmp)$")


def log(message):
    print(message, flush=True)


def version_key(text):
    return tuple(int(part) for part in text.split("."))


def host_allowed(host):
    if host in EXTRA_HOSTS:
        return True
    return any(host == d or host.endswith("." + d) for d in ALLOWED_DOMAINS)


def check_url(url):
    parts = urllib.parse.urlsplit(url)
    host = parts.hostname or ""
    if not host_allowed(host):
        raise ValueError(f"Host nicht erlaubt: {host}")
    if parts.scheme != "https" and host not in EXTRA_HOSTS:
        raise ValueError(f"nur HTTPS erlaubt: {url}")


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        check_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


OPENER = urllib.request.build_opener(SafeRedirect)


def fetch(url, limit):
    check_url(url)
    request = urllib.request.Request(
        url, headers={"User-Agent": "espwatcher-ota-pull", "Accept": "application/vnd.github+json, */*"}
    )
    with OPENER.open(request, timeout=TIMEOUT) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"Antwort groesser als {limit} Byte: {url}")
    return data


def installed_versions():
    found = []
    for name in os.listdir(FW_DIR):
        match = BIN_RE.match(name)
        if match:
            found.append(match.group(1))
    return sorted(found, key=version_key)


def write_atomic(path, data):
    tmp = os.path.join(os.path.dirname(path), "." + os.path.basename(path) + ".part")
    with open(tmp, "wb") as fh:
        fh.write(data)
    os.chmod(tmp, 0o644)
    os.replace(tmp, path)


def run_gen():
    result = subprocess.run(["python3", "-I", GEN], capture_output=True, text=True, timeout=120, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"gen_manifest.py fehlgeschlagen: {result.stderr.strip() or result.stdout.strip()}")
    log(result.stdout.strip())


def pick_assets(release):
    """Liefert (version, bin_url, sha_url) oder None."""
    assets = {a.get("name", ""): a.get("browser_download_url", "") for a in release.get("assets", [])}
    candidates = []
    for name, url in assets.items():
        match = BIN_RE.match(name)
        if match and url and assets.get(name + ".sha256"):
            candidates.append((version_key(match.group(1)), match.group(1), url, assets[name + ".sha256"]))
    if not candidates:
        return None
    candidates.sort()
    _, version, bin_url, sha_url = candidates[-1]
    return version, bin_url, sha_url


def verify(version, data, sha_text):
    expected = (sha_text.split() or [""])[0].lower()
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError("Pruefsummendatei hat kein gueltiges Format")
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise ValueError(f"SHA-256 passt nicht (erwartet {expected[:12]}..., Datei {actual[:12]}...)")
    if not MIN_SIZE <= len(data) <= MAX_SIZE:
        raise ValueError(f"Groesse {len(data)} Byte ausserhalb {MIN_SIZE}..{MAX_SIZE}")
    if data[0] != 0xE9:
        raise ValueError("keine ESP8266-Firmware (Kennbyte 0xE9 fehlt)")
    if version.encode() not in data:
        raise ValueError(f"Versionstext {version} kommt in der Datei nicht vor")


def main():
    dry_run = "--dry-run" in sys.argv[1:]

    if not os.path.isdir(FW_DIR):
        log(f"FEHLER: {FW_DIR} existiert nicht")
        return 1
    if not os.path.isfile(GEN):
        log(f"FEHLER: {GEN} fehlt (erst 'ota-tool.sh install' ausfuehren)")
        return 1

    os.makedirs(os.path.dirname(LOCK), exist_ok=True)
    lock_handle = open(LOCK, "w")
    try:
        fcntl.flock(lock_handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        log("laeuft bereits, beende")
        return 0

    for name in os.listdir(FW_DIR):  # Reste abgebrochener Laeufe
        if PART_RE.match(name):
            os.remove(os.path.join(FW_DIR, name))

    try:
        release = json.loads(fetch(f"{API}/repos/{REPO}/releases/latest", 262144))
    except urllib.error.HTTPError as exc:
        log(f"GitHub antwortet mit HTTP {exc.code} - kein Release oder Limit erreicht, spaeter erneut")
        return 0
    except (urllib.error.URLError, OSError, ValueError) as exc:
        log(f"GitHub nicht erreichbar: {exc}")
        return 0

    picked = pick_assets(release)
    if not picked:
        log(f"Release {release.get('tag_name', '?')} enthaelt keine passende firmware-<version>.bin mit .sha256")
        return 0
    version, bin_url, sha_url = picked

    have = installed_versions()
    current = have[-1] if have else None
    if current and version_key(version) <= version_key(current):
        log(f"aktuell: Pi hat {current}, neuestes Release {version}")
        return 0

    log(f"neue Firmware {version} (Pi hat {current or 'keine'})")
    if dry_run:
        log("--dry-run: nichts veraendert")
        return 0

    try:
        data = fetch(bin_url, MAX_SIZE)
        sha_text = fetch(sha_url, 1024).decode("ascii", "replace")
        verify(version, data, sha_text)
    except (urllib.error.URLError, OSError, ValueError) as exc:
        log(f"FEHLER beim Laden/Pruefen von {version}: {exc}")
        return 1

    target = os.path.join(FW_DIR, f"firmware-{version}.bin")
    try:
        write_atomic(target, data)
        write_atomic(os.path.join(FW_DIR, "firmware.bin"), data)
        run_gen()

        removed = []
        for name in os.listdir(FW_DIR):
            match = BIN_RE.match(name) or META_RE.match(name)
            if match and match.group(1) != version:
                os.remove(os.path.join(FW_DIR, name))
                removed.append(name)
        if removed:
            log("geloescht: " + ", ".join(sorted(removed)))
            run_gen()
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        log(f"FEHLER beim Veroeffentlichen: {exc}")
        return 1

    log(f"fertig: {version} liegt bereit, der ESP holt sie sich beim naechsten Check (alle 10 Minuten)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

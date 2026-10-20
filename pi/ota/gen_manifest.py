#!/usr/bin/env python3
"""Erzeugt das OTA-Manifest fuer den ESPWatcher (laeuft auf dem piholepi).

Ersetzt das alte gen_manifest.py am selben Pfad, damit esp-ota-manifest.service/.path
unveraendert weiterarbeiten. Es liest alle Dateien  firmware-<version>.bin  im
Firmware-Ordner und schreibt (jeweils atomar):

  manifest.json      klein gehalten (aeltere ESP-Firmware hat nur 768 Byte Speicher),
                     beide Feldnamen-Varianten: version/url/md5 und latest/bin/size/md5
  versions.json      Liste aller Versionen (waechst mit jedem Release, daher getrennt)
  version.txt        hoechste Version
  firmware.bin       Kopie der hoechsten Version
  firmware-<v>.json  Einzelinfo je Version

Existiert im Elternordner (/mnt/data/ota) bereits ein manifest.json bzw. version.txt,
werden diese Kopien ebenfalls aktualisiert (der alte Aufbau lieferte sie dort aus).

Umgebungsvariablen:  OTA_FW_DIR (Standard: Ordner dieses Skripts)
"""

import hashlib
import json
import os
import re
import shutil
import sys

FW_DIR = os.path.abspath(os.environ.get("OTA_FW_DIR", os.path.dirname(os.path.abspath(__file__))))
PROJECT = "ESPWatcher"
NAME_RE = re.compile(r"^firmware-(\d+(?:\.\d+)*)\.bin$")


def version_key(text):
    return tuple(int(part) for part in text.split("."))


def md5_of(path):
    digest = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_atomic(path, data, mode=0o644):
    tmp = os.path.join(os.path.dirname(path), "." + os.path.basename(path) + ".tmp")
    with open(tmp, "wb") as fh:
        fh.write(data if isinstance(data, bytes) else data.encode("utf-8"))
    os.chmod(tmp, mode)
    os.replace(tmp, path)


def copy_atomic(src, dst):
    tmp = os.path.join(os.path.dirname(dst), "." + os.path.basename(dst) + ".tmp")
    shutil.copyfile(src, tmp)
    os.chmod(tmp, 0o644)
    os.replace(tmp, dst)


def main():
    entries = []
    for name in os.listdir(FW_DIR):
        match = NAME_RE.match(name)
        if not match:
            continue
        path = os.path.join(FW_DIR, name)
        size = os.path.getsize(path)
        if size == 0:
            print(f"uebersprungen (leer): {name}", file=sys.stderr)
            continue
        entries.append({"ver": match.group(1), "bin": name, "size": size, "md5": md5_of(path)})

    if not entries:
        print(f"keine firmware-<version>.bin in {FW_DIR}", file=sys.stderr)
        return 1

    entries.sort(key=lambda e: version_key(e["ver"]))
    latest = entries[-1]

    manifest = {
        "project": PROJECT,
        # Format 1 (neu)
        "version": latest["ver"],
        "url": "/" + latest["bin"],
        # Format 2 (bisheriger Aufbau)
        "latest": latest["ver"],
        "bin": latest["bin"],
        "size": latest["size"],
        "md5": latest["md5"],
    }
    manifest_text = json.dumps(manifest, indent=2) + "\n"

    for entry in entries:
        write_atomic(os.path.join(FW_DIR, f"firmware-{entry['ver']}.json"), json.dumps(entry) + "\n")

    write_atomic(os.path.join(FW_DIR, "versions.json"), json.dumps(entries, indent=2) + "\n")
    write_atomic(os.path.join(FW_DIR, "version.txt"), latest["ver"])
    copy_atomic(os.path.join(FW_DIR, latest["bin"]), os.path.join(FW_DIR, "firmware.bin"))
    write_atomic(os.path.join(FW_DIR, "manifest.json"), manifest_text)

    parent = os.path.dirname(FW_DIR)
    if os.path.isfile(os.path.join(parent, "manifest.json")):
        write_atomic(os.path.join(parent, "manifest.json"), manifest_text)
    if os.path.isfile(os.path.join(parent, "version.txt")):
        write_atomic(os.path.join(parent, "version.txt"), latest["ver"])

    print(f"manifest: neueste Version {latest['ver']} ({latest['bin']}), {len(entries)} Version(en)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

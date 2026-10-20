#!/usr/bin/env python3
"""Test fuer espwatcher-ota-pull.py gegen eine simulierte GitHub-Schnittstelle.

Aufruf:  python3 -I test_pull.py
Braucht nur die Python-Standardbibliothek, laeuft ohne Root und ohne Netz.
"""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PULL = os.path.join(HERE, "espwatcher-ota-pull.py")
GEN = os.path.join(HERE, "gen_manifest.py")
REPO = "SpecterAoD/ESPWatcher"


def fake_firmware(version, size=349_000, magic=0xE9, include_version=True):
    body = bytearray(os.urandom(size))
    body[0] = magic
    text = f"Firmware version {version}".encode() if include_version else b"nothing here"
    body[1000:1000 + len(text)] = text
    return bytes(body)


class Github:
    """Minimale Nachbildung von api.github.com und den Asset-Downloads."""

    def __init__(self):
        self.release = None
        self.files = {}
        self.port = 0
        parent = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):  # noqa: N802
                if self.path == f"/repos/{REPO}/releases/latest":
                    if parent.release is None:
                        self.send_error(404)
                        return
                    body = json.dumps(parent.release).encode()
                elif self.path.startswith("/dl/") and self.path[4:] in parent.files:
                    body = parent.files[self.path[4:]]
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *args):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def publish(self, version, data=None, sha=None, host=None, with_sha=True, tag=None):
        data = data if data is not None else fake_firmware(version)
        name = f"firmware-{version}.bin"
        base = host or f"http://127.0.0.1:{self.port}"
        self.files[name] = data
        self.files[name + ".sha256"] = (sha or hashlib.sha256(data).hexdigest() + "  " + name + "\n").encode()
        assets = [{"name": name, "browser_download_url": f"{base}/dl/{name}"}]
        if with_sha:
            assets.append({"name": name + ".sha256", "browser_download_url": f"{base}/dl/{name}.sha256"})
        self.release = {"tag_name": tag or f"v{version}", "assets": assets}
        return data

    def close(self):
        self.server.shutdown()


class PullTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.gh = Github()

    @classmethod
    def tearDownClass(cls):
        cls.gh.close()

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ota-test-")
        self.fw = os.path.join(self.tmp, "firmwares")
        os.makedirs(os.path.join(self.fw, "old_bin"))
        shutil.copy(GEN, self.fw)
        for version in ("3.4.1", "3.4.6"):
            with open(os.path.join(self.fw, f"firmware-{version}.bin"), "wb") as fh:
                fh.write(fake_firmware(version, 330_000))
        shutil.copy(os.path.join(self.fw, "firmware-3.4.6.bin"), os.path.join(self.fw, "firmware.bin"))
        self.gh.release = None

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def run_pull(self, *args):
        env = dict(
            os.environ,
            ESPW_FW_DIR=self.fw,
            ESPW_LOCK=os.path.join(self.tmp, "pull.lock"),
            ESPW_API_BASE=f"http://127.0.0.1:{self.gh.port}",
            ESPW_ALLOW_HOSTS="127.0.0.1",
        )
        result = subprocess.run([sys.executable, "-I", PULL, *args], env=env, capture_output=True, text=True, timeout=60)
        return result.returncode, result.stdout + result.stderr

    def files(self):
        return sorted(n for n in os.listdir(self.fw) if n != "old_bin" and n != "gen_manifest.py")

    def test_neue_version_wird_installiert_und_alte_geloescht(self):
        data = self.gh.publish("3.5.0")
        code, out = self.run_pull()
        self.assertEqual(code, 0, out)
        names = self.files()
        self.assertIn("firmware-3.5.0.bin", names)
        self.assertNotIn("firmware-3.4.1.bin", names)
        self.assertNotIn("firmware-3.4.6.bin", names)
        self.assertEqual(open(os.path.join(self.fw, "firmware.bin"), "rb").read(), data)
        manifest = json.load(open(os.path.join(self.fw, "manifest.json")))
        self.assertEqual(manifest["version"], "3.5.0")
        self.assertEqual(manifest["latest"], "3.5.0")
        self.assertEqual(manifest["url"], "/firmware-3.5.0.bin")
        self.assertEqual(manifest["md5"], hashlib.md5(data).hexdigest())
        self.assertEqual([v["ver"] for v in manifest["versions"]], ["3.5.0"])
        self.assertTrue(os.path.isdir(os.path.join(self.fw, "old_bin")))
        self.assertFalse([n for n in os.listdir(self.fw) if n.endswith((".part", ".tmp"))])

    def test_gleiche_version_tut_nichts(self):
        self.gh.publish("3.4.6")
        before = self.files()
        code, out = self.run_pull()
        self.assertEqual(code, 0, out)
        self.assertIn("aktuell", out)
        self.assertEqual(self.files(), before)

    def test_aeltere_version_wird_nie_installiert(self):
        self.gh.publish("3.4.2")
        code, out = self.run_pull()
        self.assertEqual(code, 0, out)
        self.assertNotIn("firmware-3.4.2.bin", self.files())

    def test_zehn_ist_hoeher_als_neun(self):
        self.gh.publish("3.10.0")
        code, out = self.run_pull()
        self.assertEqual(code, 0, out)
        self.assertIn("firmware-3.10.0.bin", self.files())

    def test_dry_run_aendert_nichts(self):
        self.gh.publish("3.5.0")
        before = self.files()
        code, out = self.run_pull("--dry-run")
        self.assertEqual(code, 0, out)
        self.assertEqual(self.files(), before)

    def assert_abgelehnt(self, why):
        code, out = self.run_pull()
        self.assertEqual(code, 1, out)
        self.assertIn(why, out)
        self.assertEqual(self.files(), ["firmware-3.4.1.bin", "firmware-3.4.6.bin", "firmware.bin"])
        self.assertFalse(os.path.exists(os.path.join(self.fw, "manifest.json")))

    def test_falsche_pruefsumme(self):
        self.gh.publish("3.5.0", sha="0" * 64 + "  firmware-3.5.0.bin\n")
        self.assert_abgelehnt("SHA-256 passt nicht")

    def test_kaputte_pruefsummendatei(self):
        self.gh.publish("3.5.0", sha="kein hash\n")
        self.assert_abgelehnt("gueltiges Format")

    def test_keine_esp_firmware(self):
        self.gh.publish("3.5.0", data=fake_firmware("3.5.0", magic=0x7F))
        self.assert_abgelehnt("0xE9")

    def test_versionstext_fehlt(self):
        self.gh.publish("3.5.0", data=fake_firmware("3.5.0", include_version=False))
        self.assert_abgelehnt("Versionstext")

    def test_zu_gross(self):
        self.gh.publish("3.5.0", data=fake_firmware("3.5.0", size=1_200_000))
        code, out = self.run_pull()
        self.assertEqual(code, 1, out)
        self.assertEqual(self.files(), ["firmware-3.4.1.bin", "firmware-3.4.6.bin", "firmware.bin"])

    def test_zu_klein(self):
        self.gh.publish("3.5.0", data=fake_firmware("3.5.0", size=20_000))
        self.assert_abgelehnt("ausserhalb")

    def test_fremder_host_wird_nicht_geladen(self):
        self.gh.publish("3.5.0", host="http://evil.example.com")
        self.assert_abgelehnt("Host nicht erlaubt")

    def test_release_ohne_pruefsumme_wird_ignoriert(self):
        self.gh.publish("3.5.0", with_sha=False)
        code, out = self.run_pull()
        self.assertEqual(code, 0, out)
        self.assertEqual(self.files(), ["firmware-3.4.1.bin", "firmware-3.4.6.bin", "firmware.bin"])

    def test_kein_release_ist_kein_fehler(self):
        self.gh.release = None
        code, out = self.run_pull()
        self.assertEqual(code, 0, out)

    def test_github_nicht_erreichbar_ist_kein_fehler(self):
        env = dict(
            os.environ, ESPW_FW_DIR=self.fw, ESPW_LOCK=os.path.join(self.tmp, "pull.lock"),
            ESPW_API_BASE="http://127.0.0.1:9", ESPW_ALLOW_HOSTS="127.0.0.1",
        )
        result = subprocess.run([sys.executable, "-I", PULL], env=env, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("nicht erreichbar", result.stdout)

    def test_reste_abgebrochener_laeufe_werden_entfernt(self):
        open(os.path.join(self.fw, ".firmware-3.5.0.bin.part"), "wb").write(b"halb")
        self.gh.publish("3.4.6")
        code, out = self.run_pull()
        self.assertEqual(code, 0, out)
        self.assertNotIn(".firmware-3.5.0.bin.part", os.listdir(self.fw))


if __name__ == "__main__":
    unittest.main(verbosity=2)

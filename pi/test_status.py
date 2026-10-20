#!/usr/bin/env python3
"""Tests fuer espwatcher-status.py (laufen ohne Root, ohne systemd und ohne Netz).

Aufruf:  python3 -I pi/test_status.py
"""

import importlib.util
import json
import os
import socket
import struct
import threading
import types
import unittest
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))

os.environ.update(
    ESPW_BIND="127.0.0.1",
    ESPW_VIP="192.168.2.10",
    ESPW_PIZERO="127.0.0.1",
    ESPW_DNS_LOCAL="127.0.0.1",
    ESPW_DISK_PATH="/",
    ESPW_DNS_EVERY="60",
)


def load():
    spec = importlib.util.spec_from_file_location("esw", os.path.join(HERE, "espwatcher-status.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FakeDns:
    """UDP-DNS-Attrappe: antwortet mit dem gewuenschten Rcode oder gar nicht."""

    def __init__(self, rcode=0, silent=False):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.port = self.sock.getsockname()[1]
        self.rcode, self.silent, self.queries = rcode, silent, 0
        self.stop = False
        self.sock.settimeout(0.2)
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop:
            try:
                data, addr = self.sock.recvfrom(512)
            except OSError:
                continue
            self.queries += 1
            if self.silent:
                continue
            reply = data[:2] + struct.pack(">H", 0x8180 | self.rcode) + data[4:]
            self.sock.sendto(reply, addr)

    def close(self):
        self.stop = True
        self.thread.join()
        self.sock.close()


class StatusTest(unittest.TestCase):
    def setUp(self):
        self.esw = load()

    def fake_run(self, outputs):
        def run(cmd, **kwargs):
            key = cmd[-1] if cmd[0] == "systemctl" else cmd[0]
            value = outputs.get(key)
            if isinstance(value, Exception):
                raise value
            return types.SimpleNamespace(stdout=value or "", returncode=0)

        return run

    # --- Dienste ---------------------------------------------------------------
    def test_dienstzustaende(self):
        self.esw.subprocess.run = self.fake_run(
            {
                "unbound": "LoadState=loaded\nActiveState=active\n",
                "dnsdist": "LoadState=loaded\nActiveState=failed\n",
                "chrony": "LoadState=loaded\nActiveState=inactive\n",
                "fehlt": "LoadState=not-found\nActiveState=inactive\n",
                "neu": "LoadState=loaded\nActiveState=activating\n",
                "kaputt": FileNotFoundError(),
            }
        )
        expect = {"unbound": "active", "dnsdist": "failed", "chrony": "inactive",
                  "fehlt": "missing", "neu": "unknown", "kaputt": "unknown"}
        for name, state in expect.items():
            self.assertEqual(self.esw.service_state(name), state, name)

    def test_standardliste_enthaelt_dns_kette_als_kritisch(self):
        critical = {n for n, c in self.esw.DEFAULT_SERVICES if c}
        self.assertEqual(critical, {"pihole-FTL", "unbound", "dnsdist", "keepalived"})
        self.assertLessEqual(len(self.esw.DEFAULT_SERVICES), 12)  # MAX_SERVICES der Firmware
        self.assertTrue(all(len(n) <= 23 for n, _ in self.esw.DEFAULT_SERVICES))  # name[24] der Firmware

    # --- aktiver DNS -----------------------------------------------------------
    def test_vip_erkennung(self):
        mine = "2: eth0    inet 192.168.2.6/24 brd x\n2: eth0    inet 192.168.2.10/32 scope global eth0\n"
        other = "2: eth0    inet 192.168.2.6/24 brd x\n"
        self.esw.subprocess.run = self.fake_run({"ip": mine})
        self.assertTrue(self.esw.vip_is_local())
        self.esw.subprocess.run = self.fake_run({"ip": other})
        self.assertFalse(self.esw.vip_is_local())
        self.esw.subprocess.run = self.fake_run({"ip": FileNotFoundError()})
        self.assertIsNone(self.esw.vip_is_local())

    # --- DNS-Pruefung ----------------------------------------------------------
    def test_dns_antwortet(self):
        dns = FakeDns(rcode=0)
        try:
            self.esw.DNS_PORT = dns.port
            self.assertTrue(self.esw.dns_ok("127.0.0.1", timeout=1))
        finally:
            dns.close()

    def test_dns_fehler_und_schweigen(self):
        for kwargs in ({"rcode": 2}, {"silent": True}):
            dns = FakeDns(**kwargs)
            try:
                self.esw.DNS_PORT = dns.port
                self.assertFalse(self.esw.dns_ok("127.0.0.1", timeout=0.5), kwargs)
            finally:
                dns.close()

    def test_dns_pruefung_wird_gedrosselt(self):
        dns = FakeDns(rcode=0)
        try:
            self.esw.DNS_PORT = dns.port
            self.esw.VIP = self.esw.PIZERO = self.esw.DNS_LOCAL = "127.0.0.1"
            self.esw.dns_results()
            self.esw.dns_results()
            self.esw.dns_results()
            self.assertEqual(dns.queries, 3, "drei Ziele, nur eine Runde in 60 s")
        finally:
            dns.close()

    # --- Zugriffsbeschraenkung ---------------------------------------------------
    def test_nur_lan_darf_abfragen(self):
        self.assertTrue(self.esw.client_allowed("192.168.2.17"))
        self.assertTrue(self.esw.client_allowed("127.0.0.1"))
        self.assertFalse(self.esw.client_allowed("100.64.1.5"))  # Tailscale
        self.assertFalse(self.esw.client_allowed("8.8.8.8"))
        self.assertFalse(self.esw.client_allowed("kein-ip"))

    # --- Gesamtausgabe ---------------------------------------------------------
    def test_ausgabe_passt_zur_firmware(self):
        dns = FakeDns(rcode=0)
        self.esw.DNS_PORT = dns.port
        self.esw.VIP = "127.0.0.1"  # Attrappe statt der echten keepalived-Adresse
        self.esw.subprocess.run = self.fake_run(
            {
                "pihole-FTL": "LoadState=loaded\nActiveState=active\n",
                "unbound": "LoadState=loaded\nActiveState=failed\n",
                "ip": "1: lo    inet 127.0.0.1/32 scope global lo\n",
                "vcgencmd": "throttled=0x0\n",
            }
        )
        self.esw.SERVICES = [("pihole-FTL", 1), ("unbound", 1)]
        try:
            data = self.esw.collect()
        finally:
            dns.close()
        self.assertEqual(data["active"], "piholepi")
        self.assertEqual(data["dns"], {"local": 1, "vip": 1, "pizero": 1})
        self.assertEqual(data["svc"], [{"n": "pihole-FTL", "s": "active", "c": 1},
                                       {"n": "unbound", "s": "failed", "c": 1}])
        self.assertEqual(data["thr"], 0)
        self.assertIn("mounted", data["disk"])
        json.dumps(data)  # muss serialisierbar sein

    def test_http_lieferung(self):
        self.esw._snapshot.update(ts=__import__("time").time(), data={"v": 1, "active": "piholepi", "svc": []})
        server = self.esw.ThreadingHTTPServer(("127.0.0.1", 0), self.esw.Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            base = f"http://127.0.0.1:{server.server_address[1]}"
            with urllib.request.urlopen(base + "/status.json", timeout=5) as response:
                body = json.load(response)
            self.assertEqual(body["active"], "piholepi")
            self.assertIn("age_s", body)
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                urllib.request.urlopen(base + "/etc/passwd", timeout=5)
            self.assertEqual(ctx.exception.code, 404)
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    unittest.main(verbosity=2)

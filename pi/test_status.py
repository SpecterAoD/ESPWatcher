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
import time
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

    # --- Sicherungen -----------------------------------------------------------
    @staticmethod
    def timers(last=913_600, nxt=1_000_000):
        """list-timers (JSON): Zeiten in Mikrosekunden; Periode = nxt - last (Standard 86400 s)."""
        return json.dumps([{"unit": "j.timer", "activates": "j.service", "last": last * 10**6, "next": nxt * 10**6}])

    def job(self, svc, timer="LoadState=loaded\nActiveState=active\n", times=None, now=1_000_000):
        self.esw.subprocess.run = self.fake_run(
            {"j.service": svc, "j.timer": timer, "--output=json": self.timers() if times is None else times})
        return self.esw.job_state("j", now=now)

    TIMER_DAILY = "LoadState=loaded\nActiveState=active\n"

    def test_job_ok(self):
        svc = "LoadState=loaded\nActiveState=inactive\nResult=success\nExecMainExitTimestamp=@990000\n"
        self.assertEqual(self.job(svc, self.TIMER_DAILY), {"n": "j", "s": "ok", "a": 2.8})

    def test_job_ueberfaellig_nach_zwei_perioden(self):
        svc = "LoadState=loaded\nActiveState=inactive\nResult=success\nExecMainExitTimestamp=@%d\n" % (1_000_000 - 3 * 86400)
        self.assertEqual(self.job(svc, self.TIMER_DAILY)["s"], "late")

    def test_job_fehler_laeuft_fehlt_timer_aus(self):
        base = "LoadState=loaded\nActiveState=inactive\nResult=%s\nExecMainExitTimestamp=@990000\n"
        self.assertEqual(self.job(base % "exit-code", self.TIMER_DAILY)["s"], "fail")
        self.assertEqual(self.job("LoadState=loaded\nActiveState=activating\nResult=success\n", self.TIMER_DAILY)["s"], "run")
        self.assertEqual(self.job("LoadState=not-found\nActiveState=inactive\n", "")["s"], "missing")
        off = "LoadState=loaded\nActiveState=inactive\n"
        self.assertEqual(self.job(base % "success", off)["s"], "off")

    def test_job_ohne_timer_nutzt_36_stunden(self):
        old = "LoadState=loaded\nActiveState=inactive\nResult=success\nExecMainExitTimestamp=@%d\n" % (1_000_000 - 40 * 3600)
        self.assertEqual(self.job(old, "LoadState=not-found\n", times="[]")["s"], "late")

    def test_job_woechentlich_ist_nach_3_tagen_nicht_ueberfaellig(self):
        week = 7 * 86400
        svc = "LoadState=loaded\nActiveState=inactive\nResult=success\nExecMainExitTimestamp=@%d\n" % (1_000_000 - 3 * 86400)
        times = self.timers(last=1_000_000 - 3 * 86400, nxt=1_000_000 - 3 * 86400 + week)
        self.assertEqual(self.job(svc, times=times)["s"], "ok")

    def test_job_nach_neustart_nutzt_timerzeitpunkt(self):
        svc = "LoadState=loaded\nActiveState=inactive\nResult=success\nExecMainExitTimestamp=\n"
        self.assertEqual(self.job(svc, self.TIMER_DAILY), {"n": "j", "s": "ok", "a": 24.0})

    def test_job_ganz_ohne_zeitpunkt_ist_unbekannt(self):
        svc = "LoadState=loaded\nActiveState=inactive\nResult=success\nExecMainExitTimestamp=\n"
        self.assertEqual(self.job(svc, times="[]")["s"], "unknown")

    # --- Diagnose von mirror-diag -------------------------------------------------
    def test_diag_zusammenfassung(self):
        import tempfile
        raw = {
            "summary": {"dns": {"state": "gruen", "text": "VIP bei piholepi"},
                        "netz": {"state": "gelb", "text": "mesh-sz: 40 ms"},
                        "geraete": {"state": "rot", "text": "nicht erreichbar: kalipi"}},
            "d3": {"failovers": {"h24": 1, "d7": 3}},
            "d4": {"dsl": {"dsl": "online", "online": "online", "inetDown": 120000, "inetUp": 40000,
                           "firmware": "geheim?", "since": "x"},
                   "mesh": {"nodes": [{"name": "infra-mesh-wz", "connected": True, "mac": "aa"},
                                      {"name": "infra-mesh-sz", "connected": False}]}},
            "d5": {"devices": [{"name": "infra-diskstation", "state": "gelb", "online": True, "ms": 1.4,
                                "mesh": {"rssi": -71}},
                               {"name": "kalipi", "state": "rot", "online": False, "ms": None}]},
        }
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            json.dump(raw, fh)
        self.esw.DIAG_FILE = fh.name
        try:
            out = self.esw.read_diag()
        finally:
            os.unlink(fh.name)
        self.assertEqual(out["net"], {"s": "gelb", "t": "mesh-sz: 40 ms"})
        self.assertEqual(out["off"], ["kalipi"])
        self.assertEqual(out["warn"], ["diskstation"])
        self.assertEqual(out["devs"], [["infra-diskstation", 2, 1, 1, -71], ["kalipi", 3, 0, -1, 0]])
        self.assertEqual(out["n_dev"], [1, 2])
        self.assertEqual(out["wan"], {"dsl": "online", "on": "online", "down": 120000, "up": 40000})
        self.assertEqual(out["mesh"], [{"n": "mesh-wz", "c": 1}, {"n": "mesh-sz", "c": 0}])
        self.assertEqual(out["fo"], [1, 3])
        self.assertLess(out["age_s"], 5)
        self.assertNotIn("geheim", json.dumps(out))  # nur ausgewaehlte Felder werden weitergegeben

    # --- Hosts und DiskStation vom Magic Mirror -------------------------------------
    def test_homedash_hosts_und_nas(self):
        import tempfile
        now = time.time()
        raw = {
            "devices": [
                {"name": "pimirror", "ok": True, "cpu": 12.3, "temp": 51.2, "uptime": 90000,
                 "updates": 4, "security": 1, "lastUpdate": now - 7200, "reboot": True,
                 "mem": {"total": 1000, "used": 250}, "disk": {"total": 32e9, "used": 8e9},
                 "services": [{"name": "magicmirror", "state": "active"}, {"name": "x", "state": "failed"}],
                 "power": {"uvNow": False, "throttledNow": False, "hotNow": False, "uvSeen": True,
                           "throttledSeen": False}, "token": "geheim"},
                {"name": "kali", "ok": False, "error": "nicht erreichbar"},
            ],
            "nas": {"name": "DiskStation", "online": True, "raid": [{"name": "r", "status": 1}],
                    "disks": [{"name": "Disk 1", "status": 1, "temp": 38}, {"name": "Disk 2", "status": 1, "temp": None}],
                    "sysTemp": 47, "system": 1, "power": 2, "fans": [1, 1],
                    "volume": {"name": "v1", "size": 4e12, "used": 1e12}},
        }
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            json.dump(raw, fh)
        self.esw.HOMEDASH_FILE = fh.name
        try:
            out = self.esw.read_homedash()
        finally:
            os.unlink(fh.name)
        h = out["hosts"][0]
        self.assertEqual(h[:6], ["pimirror", 1, 12.3, 25.0, 25.0, 24.0])
        self.assertEqual(h[6:10], [51.2, 90000, 4, 1])
        self.assertEqual(h[10], 2)  # Stunden seit letztem Update
        self.assertEqual(h[11], 1 | 8)  # Neustart noetig + Unterspannung seit Start
        self.assertEqual(h[12], "x")
        self.assertEqual(out["hosts"][1][:2], ["kali", 0])
        self.assertEqual(out["hosts"][1][12], "nicht erreichbar")
        nas = out["nas"]
        self.assertEqual((nas["on"], nas["pwr"], nas["sys"], nas["st"]), (1, 2, 1, 47))
        self.assertEqual(nas["disks"], [["Disk 1", 1, 38], ["Disk 2", 1, -1]])
        self.assertEqual(nas["vol"], [25.0, 4000])
        self.assertNotIn("geheim", json.dumps(out))

    def test_homedash_fehlt_oder_kaputt(self):
        import tempfile
        self.esw.HOMEDASH_FILE = "/nicht/vorhanden.json"
        self.assertIsNone(self.esw.read_homedash())
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            fh.write("[1]")
        self.esw.HOMEDASH_FILE = fh.name
        try:
            self.assertEqual(self.esw.read_homedash(), {"err": "read"})
        finally:
            os.unlink(fh.name)

    def test_diag_fehlt_oder_kaputt(self):
        import tempfile
        self.esw.DIAG_FILE = "/nicht/vorhanden.json"
        self.assertIsNone(self.esw.read_diag())
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as fh:
            fh.write("{kaputt")
        self.esw.DIAG_FILE = fh.name
        try:
            self.assertEqual(self.esw.read_diag(), {"err": "read"})
        finally:
            os.unlink(fh.name)

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
        self.esw.DIAG_FILE = "/nicht/vorhanden.json"
        try:
            data = self.esw.collect()
        finally:
            dns.close()
        self.assertEqual(data["active"], "piholepi")
        self.assertEqual(data["dns"], {"local": 1, "vip": 1, "pizero": 1})
        self.assertEqual(data["svc"], [{"n": "pihole-FTL", "s": "active", "c": 1},
                                       {"n": "unbound", "s": "failed", "c": 1}])
        self.assertEqual(data["thr"], 0)
        self.assertEqual(set(data["dns_ms"]), {"local", "vip", "pizero"})
        self.assertEqual(len(data["jobs"]), len(self.esw.JOBS))
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

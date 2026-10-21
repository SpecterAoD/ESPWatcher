# Status-Dienst für den piholepi

`espwatcher-status.py` ist ein kleiner, **nur lesender** HTTP-Dienst (nur Python-Standardbibliothek).
Er prüft alle 5 Sekunden im Hintergrund und liefert das Ergebnis als JSON unter
`http://192.168.2.6:8092/status.json`. Der ESP8266 holt nur diese Datei.

Geprüft werden:

- Dienste per systemd: `pihole-FTL`, `unbound`, `dnsdist`, `keepalived` (kritisch, ESP zeigt ROT)
  sowie `nginx`, `chrony`, `netdata`, `tailscaled`, `pihole-dns-policy`, `nft-localonly` (nicht kritisch, ESP zeigt GELB)
- wer die keepalived-Adresse `192.168.2.10` gerade hält (`piholepi` oder `pizero`)
- ob DNS antwortet: lokal auf dem piholepi, auf der VIP und auf pizero (`192.168.2.5`)
- Datenplatte `/mnt/data` (eingehängt? wie voll?), CPU-Temperatur, Load, RAM,
  Unterspannung/Drosselung (`vcgencmd`), optional das Alter des letzten Backups

Die Dienstliste lässt sich in `espwatcher-status.py` (`DEFAULT_SERVICES`) oder per
`ESPW_SERVICES="name:1,name:0"` anpassen (`1` = kritisch).
Der Name der WireGuard-Einheit (z. B. `wg-quick@wg0`) ist nicht vorbelegt.

## Sicherungen, Netz und Geräte (ab 3.6.0)

- **Sicherungen:** pro Eintrag in `ESPW_JOBS` (Standard: `rsync-pull`, `restic-backup`, `raspiBackup`, `zero-backup`,
  `backup-verify`, `netalertx-db-backup`) wird der systemd-Dienst und der gleichnamige Timer gelesen: ok, überfällig,
  fehlgeschlagen, läuft, Timer aus. „Überfällig“ heißt: älter als zwei Timer-Perioden. Es werden keine Skripte geändert.
- **Netz und Geräte:** wenn `mirror-diag` läuft, liest der Dienst dessen `/run/mirror-diag/diag.json` (nur lesend, nur
  ausgewählte Felder: Zusammenfassungen, nicht erreichbare Geräte, Mesh-Knoten, Internet/DSL, DNS-Umschaltungen).
  Das Router-Passwort wird dafür nicht gebraucht und nie weitergegeben. Fehlt die Datei, bleibt der Bereich weg.
- **Hosts und DiskStation:** liest zusätzlich `/run/mirror-diag/homedash.json` (nur lesend; Pfad per
  `ESPW_HOMEDASH_FILE`) und gibt die Hosts-Tabelle des Magic Mirrors (CPU, RAM, Speicher, Temperatur, Updates,
  Dienste, Unterspannung) sowie den DiskStation-Zustand (RAID, Platten, Temperaturen, Netzteil) weiter.
  Zugangsdaten stehen dort nicht und werden nicht weitergegeben.
- **Links und Internet-Test:** Die Links im Reiter „Links“ stehen in `/etc/espwatcher/links.json`
  (`status-tool.sh install` legt sie einmal an und überschreibt eigene Änderungen nie). Der Dienst prüft jeden Link per
  TCP und der ESP zeigt ihn grün oder rot. Zusätzlich misst der Pi die Verbindung ins Internet (`ESPW_WAN_TARGETS`).
- **DNS-Antwortzeit:** lokal, VIP und pizero; über 800 ms zeigt der ESP gelb.

## Installation (auf dem piholepi)

```bash
git clone --depth 1 https://github.com/SpecterAoD/ESPWatcher.git ~/espwatcher-src
cd ~/espwatcher-src/pi
sudo ./status-tool.sh install
```

Das Werkzeug prüft vorher Python, systemd, die Adresse `192.168.2.6` und den freien Port 8092, legt den Benutzer
`espwatcher` an, installiert Skript und Dienst, startet ihn und zeigt die erste Antwort. Antwortet der Dienst nicht,
stellt es den vorherigen Stand wieder her. Weitere Befehle: `status`, `uninstall`.

Der Dienst beantwortet nur Anfragen aus dem LAN (`ESPW_ALLOW`, Standard `192.168.2.0/24`); alle anderen bekommen 403.
Die DNS-Prüfungen laufen nur einmal pro Minute (`ESPW_DNS_EVERY`), damit sie die Pi-hole-Statistik nicht verfälschen.

Ist `ufw` aktiv, gibt `install` den TCP-Port 8092 für 192.168.2.0/24 frei (`uninstall` entfernt die Regel). Der Dienst lauscht nur auf `192.168.2.6`, nicht auf Tailscale.

Optional: Zeigt `ESPW_BACKUP_MARKER` auf eine Datei, die dein Backup-Skript nach jedem
erfolgreichen Lauf mit `touch` aktualisiert, zeigt der ESP das Alter des letzten Backups an
und warnt ab 36 Stunden.

## Rückgängig machen

```bash
sudo ./status-tool.sh uninstall
```

## Beispiel-Antwort

```json
{"v":1,"host":"piholepi","active":"piholepi",
 "dns":{"local":1,"vip":1,"pizero":1},
 "svc":[{"n":"pihole-FTL","s":"active","c":1},{"n":"unbound","s":"active","c":1}],
 "disk":{"mounted":true,"used":42.0,"free_gb":812.3},
 "temp":48.3,"load1":0.12,"mem":31.5,"thr":0,"backup_h":5.2,
 "ts":1760000000,"age_s":2.1}
```

## Tests

`python3 -I test_status.py` prüft Dienstzustände, Erkennung der VIP, DNS-Prüfung (gegen eine Attrappe), Drosselung, LAN-Beschränkung und das JSON. Der GitHub-Build führt ihn mit aus.

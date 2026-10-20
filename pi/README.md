# Status-Dienst für den piholepi

`espwatcher-status.py` ist ein kleiner, **nur lesender** HTTP-Dienst (nur Python-Standardbibliothek).
Er prüft alle 5 Sekunden im Hintergrund und liefert das Ergebnis als JSON unter
`http://192.168.2.6:8092/status.json`. Der ESP8266 holt nur diese Datei.

Geprüft werden:

- Dienste per systemd: `pihole-FTL`, `unbound`, `dnsdist`, `keepalived` (kritisch, ESP zeigt ROT)
  sowie `chrony`, `netdata`, `tailscaled` (nicht kritisch, ESP zeigt GELB)
- wer die keepalived-Adresse `192.168.2.10` gerade hält (`piholepi` oder `pizero`)
- ob DNS antwortet: lokal auf dem piholepi, auf der VIP und auf pizero (`192.168.2.5`)
- Datenplatte `/mnt/data` (eingehängt? wie voll?), CPU-Temperatur, Load, RAM,
  Unterspannung/Drosselung (`vcgencmd`), optional das Alter des letzten Backups

Die Dienstliste lässt sich in `espwatcher-status.py` (`DEFAULT_SERVICES`) oder per
`ESPW_SERVICES="name:1,name:0"` anpassen (`1` = kritisch).
Der Name der WireGuard-Einheit (z. B. `wg-quick@wg0`) ist nicht vorbelegt.

## Installation (auf dem piholepi, mit Root-Rechten)

```bash
# 1. Eigener Benutzer ohne Login und ohne Rechte
sudo useradd --system --no-create-home --shell /usr/sbin/nologin --groups video espwatcher

# 2. Skript und Dienst installieren
sudo install -m 0755 espwatcher-status.py /usr/local/bin/espwatcher-status.py
sudo install -m 0644 espwatcher-status.service /etc/systemd/system/espwatcher-status.service

# 3. Starten
sudo systemctl daemon-reload
sudo systemctl enable --now espwatcher-status.service

# 4. Testen
curl -s http://192.168.2.6:8092/status.json | python3 -m json.tool
```

Falls auf dem Pi eine Firewall läuft (ufw/nftables), muss der TCP-Port 8092 aus dem LAN
(192.168.2.0/24) erreichbar sein. Der Dienst lauscht nur auf `192.168.2.6`, nicht auf Tailscale.

Optional: Zeigt `ESPW_BACKUP_MARKER` auf eine Datei, die dein Backup-Skript nach jedem
erfolgreichen Lauf mit `touch` aktualisiert, zeigt der ESP das Alter des letzten Backups an
und warnt ab 36 Stunden.

## Rückgängig machen

```bash
sudo systemctl disable --now espwatcher-status.service
sudo rm /etc/systemd/system/espwatcher-status.service /usr/local/bin/espwatcher-status.py
sudo userdel espwatcher
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

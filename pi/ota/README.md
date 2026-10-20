# OTA-Server (nginx auf dem piholepi)

Der ESPWatcher holt Updates über `http://192.168.2.6:8091/manifest.json`. nginx liefert dort nur
`manifest.json` und `firmware.bin` aus, schreibgeschützt und nur aus dem LAN.

## Einmalig einrichten (auf dem piholepi)

```bash
sudo install -d -m 0755 /srv/espwatcher-ota
sudo install -m 0644 espwatcher-ota.nginx.conf /etc/nginx/conf.d/espwatcher-ota.conf
sudo nginx -t && sudo systemctl reload nginx
```

Prüfen, ob Port 8091 frei ist: `ss -ltn | grep 8091` (sollte nichts zeigen, bevor nginx neu geladen wird).
Läuft eine Firewall (ufw/nftables), muss TCP 8091 aus `192.168.2.0/24` erlaubt sein.

## Neue Firmware ausrollen

1. Auf GitHub den Build abwarten und das Artifact `firmware` herunterladen (enthält `firmware.bin` und `manifest.json`).
2. Entpackt auf den Pi kopieren und ausführen:

   ```bash
   sudo ./deploy-ota.sh /pfad/zum/entpackten/ordner
   ```

3. Auf der Seite des ESP (`http://192.168.2.17`) bei „Update" Auto-OTA einschalten. Der ESP prüft alle 10 Minuten
   und installiert nur, wenn die Manifest-Version höher ist als die laufende. Danach Auto-OTA wieder ausschalten.

## Testen

```bash
curl -s http://192.168.2.6:8091/manifest.json
curl -sI http://192.168.2.6:8091/firmware.bin | head -3
curl -s -o /dev/null -w '%{http_code}\n' http://192.168.2.6:8091/anderes   # erwartet: 404
```

## Rückgängig machen

```bash
sudo rm /etc/nginx/conf.d/espwatcher-ota.conf
sudo nginx -t && sudo systemctl reload nginx
sudo rm -r /srv/espwatcher-ota
```

## Hinweis zur Sicherheit

Der ESP lädt per einfachem HTTP und prüft nur die MD5-Summe. Das schützt vor kaputten Downloads, nicht vor
Manipulation. Deshalb nur im LAN erreichbar machen (siehe `allow`/`deny` in der nginx-Konfiguration).

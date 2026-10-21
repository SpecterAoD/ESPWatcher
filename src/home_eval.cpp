#include "home_eval.h"

namespace {
void bumpLevel(uint8_t& lvl, uint8_t to) {
  if (to > lvl) lvl = to;
}

void append(String* note, const String& text) {
  if (!note) return;
  if (note->length()) *note += " · ";
  *note += text;
}

uint8_t thresh(float v, float warn, float crit) {
  if (v < 0) return 1;
  return v >= crit ? 3 : (v >= warn ? 2 : 1);
}
}  // namespace

const char* raidText(int8_t code) {
  switch (code) {
    case 1: return "in Ordnung";
    case 2: return "repariert";
    case 3: return "migriert";
    case 4: return "erweitert";
    case 6: return "prüft";
    case 7: return "baut auf";
    case 11: return "degradiert";
    case 12: return "abgestürzt";
    default: return "unbekannt";
  }
}

uint8_t hostLevel(const HostEntry& h, String* note) {
  uint8_t lvl = 1;
  if (!h.ok) {
    append(note, h.bad[0] ? String(h.bad) : String(F("keine Antwort")));
    return 3;
  }
  if (h.bad[0]) {
    bumpLevel(lvl, 3);
    append(note, String(h.bad) + F(" läuft nicht"));
  }
  if (h.flags & 2) {
    bumpLevel(lvl, 3);
    append(note, String(F("Unterspannung!")));
  }
  if (h.flags & 4) {
    bumpLevel(lvl, 3);
    append(note, String(F("gedrosselt")));
  }
  if ((h.flags & 8) && !(h.flags & 6)) {
    bumpLevel(lvl, 2);
    append(note, String(F("Unterspannung/Drosselung seit Start")));
  }
  if (h.flags & 1) {
    bumpLevel(lvl, 2);
    append(note, String(F("Neustart nötig")));
  }
  if (h.security > 0) {
    bumpLevel(lvl, 3);
    append(note, String(h.security) + F(" Sicherheitsupdates"));
  }
  bumpLevel(lvl, thresh(h.cpu, 70, 90));
  bumpLevel(lvl, thresh(h.mem, 85, 95));
  bumpLevel(lvl, thresh(h.disk, 80, 90));
  bumpLevel(lvl, thresh(h.temp, 70, 80));
  if (thresh(h.temp, 70, 80) > 1) append(note, String(F("Temperatur ")) + String(h.temp, 0) + " °C");
  if (thresh(h.disk, 80, 90) > 1) append(note, String(F("Speicher ")) + String(h.disk, 0) + " %");
  return lvl;
}

uint8_t nasLevel(const NasInfo& n, String* note) {
  uint8_t lvl = 1;
  if (!n.present) return 0;
  if (!n.online) {
    append(note, n.err[0] ? String(n.err) : String(F("offline")));
    return 3;
  }
  for (uint8_t i = 0; i < n.raidCount; ++i) {
    if (n.raid[i] != 1) {
      // Reparatur/Erweiterung/Pruefung laeuft: Warnung, degradiert/abgestuerzt: rot
      bumpLevel(lvl, (n.raid[i] == 11 || n.raid[i] == 12 || n.raid[i] < 0) ? 3 : 2);
      append(note, String(F("RAID ")) + raidText(n.raid[i]));
    }
  }
  for (uint8_t i = 0; i < n.diskCount; ++i) {
    if (n.disks[i].status >= 0 && n.disks[i].status != 1) {
      bumpLevel(lvl, 3);
      append(note, String(n.disks[i].name) + F(" gestört"));
    }
    bumpLevel(lvl, thresh(n.disks[i].temp, 45, 50));
  }
  if (n.sys >= 0 && n.sys != 1) {
    bumpLevel(lvl, 3);
    append(note, String(F("System gestört")));
  }
  if (n.power >= 0 && n.power != 1) {
    bumpLevel(lvl, 3);
    append(note, String(F("Netzteil gestört")));
  }
  for (uint8_t i = 0; i < n.fanCount; ++i) {
    if (n.fans[i] >= 0 && n.fans[i] != 1) {
      bumpLevel(lvl, 3);
      append(note, String(F("Lüfter gestört")));
      break;
    }
  }
  bumpLevel(lvl, thresh(n.sysTemp, 55, 65));
  if (n.volUsedPct >= 0) bumpLevel(lvl, thresh(n.volUsedPct, 85, 95));
  return lvl;
}

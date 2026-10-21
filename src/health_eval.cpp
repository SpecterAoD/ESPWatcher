#include "health_eval.h"

#include "status_client.h"

namespace {
struct Collector {
  HealthState level = HealthState::GREEN;
  String text;

  void add(HealthState lvl, const String& msg) {
    if (static_cast<uint8_t>(lvl) > static_cast<uint8_t>(level)) {
      level = lvl;
    }
    if (text.length() < 160) {
      if (text.length()) text += "; ";
      text += msg;
    }
  }
};
}  // namespace

HealthState evaluateHealth(const MetricPoint& point, const StatusPoint& status, String* reason) {
  Collector c;

  if (!point.netdataReachable && !status.reachable) {
    c.add(HealthState::RED, "piholepi nicht erreichbar");
  } else {
    if (!point.netdataReachable) {
      c.add(HealthState::YELLOW, "Netdata antwortet nicht");
    }
    if (!status.reachable) {
      c.add(HealthState::YELLOW, "Status-JSON antwortet nicht");
    }
  }

  if (status.reachable) {
    if (status.stale) {
      c.add(HealthState::YELLOW, "Status-Daten veraltet");
    }

    for (uint8_t i = 0; i < status.serviceCount; ++i) {
      const ServiceEntry& s = status.services[i];
      if (s.state == SvcState::FAILED || s.state == SvcState::INACTIVE) {
        c.add(s.critical ? HealthState::RED : HealthState::YELLOW, String(s.name) + ": " + svcStateToText(s.state));
      }
    }

    if (status.activeDns == "pizero") {
      c.add(HealthState::YELLOW, "DNS aktiv auf pizero (Failover)");
    } else if (status.activeDns != "piholepi") {
      c.add(HealthState::YELLOW, "aktiver DNS unbekannt");
    }

    if (status.dnsLocal == 0) c.add(HealthState::RED, "DNS auf piholepi antwortet nicht");
    if (status.dnsVip == 0) c.add(HealthState::RED, "DNS-VIP antwortet nicht");
    if (status.dnsPizero == 0) c.add(HealthState::YELLOW, "DNS auf pizero antwortet nicht");

    const int16_t slow = max(status.dnsMsLocal, max(status.dnsMsVip, status.dnsMsPizero));
    if (slow > 800) {
      c.add(HealthState::YELLOW, String("DNS antwortet langsam (") + slow + " ms)");
    }

    for (uint8_t i = 0; i < status.jobCount; ++i) {
      const JobEntry& j = status.jobs[i];
      if (j.state == JobState::FAIL) {
        c.add(HealthState::YELLOW, String(j.name) + ": fehlgeschlagen");
      } else if (j.state == JobState::LATE) {
        c.add(HealthState::YELLOW, String(j.name) + ": überfällig (" + String(j.ageH, 0) + " h)");
      } else if (j.state == JobState::OFF) {
        c.add(HealthState::YELLOW, String(j.name) + ": Timer aus");
      }
    }

    const DiagInfo& d = status.diag;
    if (d.present) {
      if (d.ageS > 180) {
        c.add(HealthState::YELLOW, "Netz-Diagnose veraltet");
      }
      if ((d.wanDsl.length() && d.wanDsl != "online") || (d.wanOnline.length() && d.wanOnline != "online")) {
        c.add(HealthState::RED, "Internet/DSL nicht online");
      }
      if (d.netLevel == 3) {
        c.add(HealthState::YELLOW, String("Netz: ") + d.netText);
      }
      if (d.devLevel == 3 && d.offline.length()) {
        c.add(HealthState::YELLOW, String("Geräte offline: ") + d.offline);
      }
      for (uint8_t i = 0; i < d.meshCount; ++i) {
        if (!d.mesh[i].connected) {
          c.add(HealthState::YELLOW, String("Mesh ") + d.mesh[i].name + " getrennt");
        }
      }
    }

    if (status.diskMounted == 0) {
      c.add(HealthState::YELLOW, "Datenplatte nicht eingehängt");
    } else if (!isnan(status.diskUsedPct) && status.diskUsedPct > 90.0f) {
      c.add(HealthState::YELLOW, String("Datenplatte ") + String(status.diskUsedPct, 0) + "% voll");
    }

    if (!isnan(status.backupAgeH) && status.backupAgeH > 36.0f) {
      c.add(HealthState::YELLOW, String("Backup ") + String(status.backupAgeH, 0) + " h alt");
    }

    if (status.throttled == 1) {
      c.add(HealthState::YELLOW, "Pi gedrosselt/Unterspannung");
    }
  }

  if (point.netdataReachable) {
    if (point.criticalAlerts > 0) {
      c.add(HealthState::RED, String(point.criticalAlerts) + " kritische Netdata-Alarme");
    } else if (point.warningAlerts > 0) {
      c.add(HealthState::YELLOW, String(point.warningAlerts) + " Netdata-Warnungen");
    }
  }

  if (!isnan(point.temperature)) {
    if (point.temperature > 80.0f) {
      c.add(HealthState::RED, String("CPU ") + String(point.temperature, 0) + " °C");
    } else if (point.temperature > 70.0f) {
      c.add(HealthState::YELLOW, String("CPU ") + String(point.temperature, 0) + " °C");
    }
  }

  if (reason) {
    *reason = c.text;
  }
  return c.level;
}

const char* healthToText(HealthState state) {
  switch (state) {
    case HealthState::GREEN:
      return "GREEN";
    case HealthState::YELLOW:
      return "YELLOW";
    case HealthState::RED:
      return "RED";
    default:
      return "UNKNOWN";
  }
}

const char* healthToGerman(HealthState state) {
  switch (state) {
    case HealthState::GREEN:
      return "Alles in Ordnung";
    case HealthState::YELLOW:
      return "Achtung";
    case HealthState::RED:
      return "Störung";
    default:
      return "Unbekannt";
  }
}

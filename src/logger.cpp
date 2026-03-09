#include "logger.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

namespace {
WiFiUDP syslogUdp;
IPAddress syslogIp;
uint16_t syslogPort = 514;
String hostName = "espwatcher";
bool syslogEnabled = false;
bool syslogErrorReported = false;

const char* levelToText(LogLevel level) {
  switch (level) {
    case LogLevel::WARN:
      return "WARN";
    case LogLevel::ERR:
      return "ERR";
    case LogLevel::INFO:
    default:
      return "INFO";
  }
}

uint8_t levelToPri(LogLevel level) {
  switch (level) {
    case LogLevel::WARN:
      return 4;
    case LogLevel::ERR:
      return 3;
    case LogLevel::INFO:
    default:
      return 6;
  }
}

void emitSerial(const char* tag, const char* text, LogLevel level) {
  Serial.printf("[%s][%s] %s\n", tag, levelToText(level), text);
}

void emitSyslog(const char* tag, const char* text, LogLevel level) {
  if (!syslogEnabled || WiFi.status() != WL_CONNECTED) {
    return;
  }

  constexpr uint8_t facility = 1;
  const uint8_t pri = static_cast<uint8_t>(facility * 8 + levelToPri(level));

  char packet[256];
  int written = snprintf(packet, sizeof(packet), "<%u>%s %s: [%s] %s", pri, hostName.c_str(), levelToText(level), tag, text);
  if (written <= 0) {
    return;
  }

  if (!syslogUdp.beginPacket(syslogIp, syslogPort)) {
    if (!syslogErrorReported) {
      emitSerial("SYSLOG", "UDP beginPacket failed", LogLevel::WARN);
      syslogErrorReported = true;
    }
    return;
  }

  size_t sent = syslogUdp.write(reinterpret_cast<const uint8_t*>(packet),
                                static_cast<size_t>(written >= static_cast<int>(sizeof(packet)) ? sizeof(packet) - 1 : written));
  bool ok = sent > 0 && syslogUdp.endPacket();
  if (!ok) {
    if (!syslogErrorReported) {
      emitSerial("SYSLOG", "UDP send failed", LogLevel::WARN);
      syslogErrorReported = true;
    }
    return;
  }

  syslogErrorReported = false;
}
}  // namespace

void loggerSetup(const char* syslogHost, uint16_t port, const char* hostname) {
  syslogPort = port;
  hostName = hostname;
  syslogEnabled = WiFi.hostByName(syslogHost, syslogIp) == 1;

  if (!syslogEnabled) {
    logMessage("SYSLOG", "Failed to resolve syslog host; serial logging only", LogLevel::WARN);
  } else {
    logMessage("SYSLOG", String("Syslog target ") + syslogIp.toString() + ":" + String(syslogPort));
  }
}

void logMessage(const char* tag, const String& message, LogLevel level) {
  logMessage(tag, message.c_str(), level);
}

void logMessage(const char* tag, const char* message, LogLevel level) {
  emitSerial(tag, message, level);
  emitSyslog(tag, message, level);
}

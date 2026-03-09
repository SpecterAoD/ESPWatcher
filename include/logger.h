#pragma once

#include <Arduino.h>

enum class LogLevel : uint8_t { INFO = 0, WARN = 1, ERR = 2 };

void loggerSetup(const char* syslogHost, uint16_t syslogPort, const char* hostname);
void logMessage(const char* tag, const String& message, LogLevel level = LogLevel::INFO);
void logMessage(const char* tag, const char* message, LogLevel level = LogLevel::INFO);

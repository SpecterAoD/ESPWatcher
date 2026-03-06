#include "netdata_client.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>

#include "config.h"

namespace {
String netdataUrl(const char* pathAndQuery) {
  String url = "http://";
  url += NETDATA_HOST;
  url += ":";
  url += String(NETDATA_PORT);
  url += pathAndQuery;
  return url;
}

bool fetchJson(const char* pathAndQuery, DynamicJsonDocument& doc) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, netdataUrl(pathAndQuery))) {
    return false;
  }
  http.setTimeout(NETDATA_TIMEOUT_MS);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  return !err;
}

float parseChartValue(JsonVariantConst root, const char* preferredDim) {
  JsonArrayConst labels = root["labels"].as<JsonArrayConst>();
  JsonArrayConst values = root["data"][0].as<JsonArrayConst>();
  if (labels.isNull() || values.isNull()) {
    return NAN;
  }

  int preferredIndex = -1;
  for (size_t i = 0; i < labels.size(); ++i) {
    const char* label = labels[i] | "";
    if (strcmp(label, preferredDim) == 0) {
      preferredIndex = static_cast<int>(i);
      break;
    }
  }

  if (preferredIndex >= 0 && static_cast<size_t>(preferredIndex) < values.size()) {
    return values[preferredIndex].as<float>();
  }

  for (size_t i = 1; i < values.size(); ++i) {
    if (!values[i].isNull()) {
      return values[i].as<float>();
    }
  }

  return NAN;
}
}  // namespace

bool netdataPoll(MetricPoint& outPoint) {
  outPoint = MetricPoint{};
  outPoint.timestampMs = millis();

  DynamicJsonDocument doc(2048);

  bool okCpu = fetchJson("/api/v1/data?chart=system.cpu&points=1", doc);
  if (okCpu) {
    outPoint.cpuUsage = parseChartValue(doc.as<JsonVariantConst>(), "user");
  }

  doc.clear();
  bool okRam = fetchJson("/api/v1/data?chart=system.ram&points=1", doc);
  if (okRam) {
    JsonArrayConst labels = doc["labels"].as<JsonArrayConst>();
    JsonArrayConst values = doc["data"][0].as<JsonArrayConst>();
    float used = NAN;
    float free = NAN;
    for (size_t i = 0; i < labels.size() && i < values.size(); ++i) {
      const char* label = labels[i] | "";
      if (strcmp(label, "used") == 0) {
        used = values[i].as<float>();
      } else if (strcmp(label, "free") == 0) {
        free = values[i].as<float>();
      }
    }
    if (!isnan(used) && !isnan(free) && (used + free) > 0.0f) {
      outPoint.ramUsage = used * 100.0f / (used + free);
    }
  }

  doc.clear();
  bool okTemp = fetchJson("/api/v1/data?chart=sensors.temp&points=1", doc);
  if (okTemp) {
    outPoint.temperature = parseChartValue(doc.as<JsonVariantConst>(), "temp");
  } else {
    outPoint.unavailableMetrics += "temperature ";
  }

  doc.clear();
  bool okLoad = fetchJson("/api/v1/data?chart=system.load&points=1", doc);
  if (okLoad) {
    outPoint.load1 = parseChartValue(doc.as<JsonVariantConst>(), "load1");
  }

  doc.clear();
  bool okAlarms = fetchJson("/api/v1/alarms", doc);
  if (okAlarms) {
    JsonObjectConst alarms = doc["alarms"].as<JsonObjectConst>();
    for (JsonPairConst kv : alarms) {
      const char* status = kv.value()["status"] | "";
      if (strcmp(status, "CRITICAL") == 0) {
        outPoint.criticalAlerts++;
      } else if (strcmp(status, "WARNING") == 0) {
        outPoint.warningAlerts++;
      }
    }
    outPoint.alertSummary = String("W:") + outPoint.warningAlerts + " C:" + outPoint.criticalAlerts;
  } else {
    outPoint.alertSummary = "alarms unavailable";
  }

  doc.clear();
  bool okThrottle = fetchJson("/api/v1/data?chart=raspberry_pi.throttled&points=1", doc);
  if (okThrottle) {
    float value = parseChartValue(doc.as<JsonVariantConst>(), "throttled");
    outPoint.throttledAvailable = !isnan(value);
    outPoint.throttled = value > 0.5f;
  } else {
    outPoint.unavailableMetrics += "throttle_status ";
  }

  outPoint.netdataReachable = okCpu || okRam || okTemp || okLoad || okAlarms;
  return outPoint.netdataReachable;
}

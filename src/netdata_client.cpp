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

// Holt JSON; optional mit ArduinoJson-Filter, damit grosse Antworten
// (z. B. /api/v1/alarms) nicht den Speicher sprengen.
bool fetchJson(const String& pathAndQuery, DynamicJsonDocument& doc, const JsonDocument* filter = nullptr) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, netdataUrl(pathAndQuery.c_str()))) {
    return false;
  }
  // HTTP/1.0 verhindert "chunked"-Antworten, die getStream() nicht dekodiert.
  http.useHTTP10(true);
  http.setTimeout(NETDATA_TIMEOUT_MS);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  DeserializationError err = filter
                                 ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
                                 : deserializeJson(doc, http.getStream());
  http.end();
  return !err;
}

// Wert einer bestimmten Dimension (Spalte "label"), sonst erster nicht-leerer Wert.
float parseChartValue(JsonVariantConst root, const char* preferredDim) {
  JsonArrayConst labels = root["labels"].as<JsonArrayConst>();
  JsonArrayConst values = root["data"][0].as<JsonArrayConst>();
  if (labels.isNull() || values.isNull()) {
    return NAN;
  }

  for (size_t i = 0; i < labels.size() && i < values.size(); ++i) {
    const char* label = labels[i] | "";
    if (strcmp(label, preferredDim) == 0 && !values[i].isNull()) {
      return values[i].as<float>();
    }
  }

  for (size_t i = 1; i < values.size(); ++i) {
    if (!values[i].isNull()) {
      return values[i].as<float>();
    }
  }

  return NAN;
}

// Summe aller Dimensionen (Spalte 0 ist die Zeit) und ggf. Wert einer Dimension.
bool sumChart(JsonVariantConst root, const char* pickDim, float& sum, float& picked) {
  JsonArrayConst labels = root["labels"].as<JsonArrayConst>();
  JsonArrayConst values = root["data"][0].as<JsonArrayConst>();
  if (labels.isNull() || values.isNull()) {
    return false;
  }

  sum = 0.0f;
  picked = NAN;
  for (size_t i = 1; i < values.size() && i < labels.size(); ++i) {
    if (values[i].isNull()) continue;
    const float v = values[i].as<float>();
    sum += v;
    if (pickDim && strcmp(labels[i] | "", pickDim) == 0) {
      picked = v;
    }
  }
  return true;
}
}  // namespace

bool netdataPoll(MetricPoint& outPoint) {
  outPoint = MetricPoint{};
  outPoint.timestampMs = millis();

  DynamicJsonDocument doc(2048);

  // CPU zuerst: Antwortet Netdata gar nicht, brechen wir sofort ab, statt
  // jede weitere Abfrage einzeln in den Timeout laufen zu lassen.
  bool okCpu = fetchJson("/api/v1/data?chart=system.cpu&points=1", doc);
  if (!okCpu) {
    outPoint.alertSummary = "Netdata nicht erreichbar";
    return false;
  }
  {
    // system.cpu enthaelt nur belegte Anteile (kein "idle") -> Summe = Auslastung.
    float sum = 0.0f;
    float unused = NAN;
    if (sumChart(doc.as<JsonVariantConst>(), nullptr, sum, unused)) {
      outPoint.cpuUsage = constrain(sum, 0.0f, 100.0f);
    }
  }

  doc.clear();
  bool okRam = fetchJson("/api/v1/data?chart=system.ram&points=1", doc);
  if (okRam) {
    float total = 0.0f;
    float used = NAN;
    if (sumChart(doc.as<JsonVariantConst>(), "used", total, used) && total > 0.0f && !isnan(used)) {
      outPoint.ramUsage = used * 100.0f / total;
    }
  }

  doc.clear();
  String tempPath = String("/api/v1/data?chart=") + NETDATA_TEMP_CHART + "&points=1";
  bool okTemp = fetchJson(tempPath, doc);
  if (okTemp) {
    outPoint.temperature = parseChartValue(doc.as<JsonVariantConst>(), "temp");
  }
  if (!okTemp || isnan(outPoint.temperature)) {
    outPoint.unavailableMetrics += "Temperatur ";
  }

  doc.clear();
  bool okLoad = fetchJson("/api/v1/data?chart=system.load&points=1", doc);
  if (okLoad) {
    outPoint.load1 = parseChartValue(doc.as<JsonVariantConst>(), "load1");
  }

  doc.clear();
  StaticJsonDocument<128> filter;
  filter["alarms"]["*"]["status"] = true;
  bool okAlarms = fetchJson("/api/v1/alarms", doc, &filter);
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
    outPoint.alertSummary = String("Warnungen: ") + outPoint.warningAlerts + ", kritisch: " + outPoint.criticalAlerts;
  } else {
    outPoint.alertSummary = "Alarme nicht verfügbar";
  }

  outPoint.netdataReachable = true;
  return true;
}

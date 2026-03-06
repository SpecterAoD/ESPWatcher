#include "ota_http.h"
#include "config.h"
#include "version.h"
#include "state.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Updater.h>   // Update.begin / write / end / setMD5

// ---------------- JSON mini parser ----------------
static String jsonGetString(const String& json, const char* key) {
  String pat = String("\"") + key + "\":";
  int i = json.indexOf(pat);
  if (i < 0) return "";
  i += pat.length();
  while (i < (int)json.length() && (json[i] == ' ')) i++;
  if (i >= (int)json.length()) return "";

  if (json[i] == '\"') {
    int j = json.indexOf('\"', i + 1);
    if (j < 0) return "";
    return json.substring(i + 1, j);
  }
  int j = i;
  while (j < (int)json.length() && json[j] != ',' && json[j] != '}' && json[j] != '\n' && json[j] != '\r') j++;
  String out = json.substring(i, j);
  out.trim();
  return out;
}

static uint32_t jsonGetU32(const String& json, const char* key) {
  String s = jsonGetString(json, key);
  s.trim();
  return (uint32_t)s.toInt();
}

static bool versionGreater(const String& a, const String& b) {
  auto nextPart = [](const String& s, int& pos)->int{
    if (pos >= (int)s.length()) return 0;
    int start = pos;
    while (pos < (int)s.length() && s[pos] != '.') pos++;
    int v = s.substring(start, pos).toInt();
    if (pos < (int)s.length() && s[pos] == '.') pos++;
    return v;
  };

  int pa = 0, pb = 0;
  for (int k = 0; k < 4; k++) {
    int va = nextPart(a, pa);
    int vb = nextPart(b, pb);
    if (va > vb) return true;
    if (va < vb) return false;
  }
  return false;
}

static String httpGet(const String& url) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);

  if (!http.begin(client, url)) return "";
  int code = http.GET();
  if (code != 200) { http.end(); return ""; }
  String body = http.getString();
  http.end();
  return body;
}

// versions.json parsen (wir suchen wiederholt Objekte mit ver/bin/md5/size)
static void parseVersions(State& s, const String& json) {
  s.otaItemCount = 0;
  int pos = 0;

  while (s.otaItemCount < State::OTA_MAX_ITEMS) {
    int vpos = json.indexOf("\"ver\"", pos);
    if (vpos < 0) break;

    int objStart = json.lastIndexOf('{', vpos);
    int objEnd   = json.indexOf('}', vpos);
    if (objStart < 0 || objEnd < 0 || objEnd <= objStart) break;

    String obj = json.substring(objStart, objEnd + 1);

    String ver = jsonGetString(obj, "ver");
    String bin = jsonGetString(obj, "bin");
    String md5 = jsonGetString(obj, "md5");
    uint32_t size = jsonGetU32(obj, "size");

    if (ver.length() && bin.length() && md5.length()) {
      State::OtaItem &it = s.otaItems[s.otaItemCount++];
      it.ver  = ver;
      it.bin  = bin;
      it.md5  = md5;
      it.size = size;
    }

    pos = objEnd + 1;
  }
}

static bool findRollbackItem(const State& s, const String& ver, State::OtaItem& out) {
  for (uint8_t i = 0; i < s.otaItemCount; i++) {
    if (s.otaItems[i].ver == ver) { out = s.otaItems[i]; return true; }
  }
  return false;
}

// ---------------- NON-BLOCKING OTA state ----------------
static HTTPClient gHttp;
static WiFiClient gClient;
static bool gHttpOpen = false;
static uint32_t gLastChunkMs = 0;

static void otaResetTransfer(State& s) {
  if (gHttpOpen) {
    gHttp.end();
    gHttpOpen = false;
  }
  gClient.stop();
  Update.end(false);

  s.otaBytesCur = 0;
  s.otaBytesTotal = 0;
  s.otaPct = 0;
  s.otaStage = State::OTA_IDLE;
}

static void otaFail(State& s, const String& msg) {
  s.otaLastMsg = msg;
  s.otaBusy = false;
  s.otaStage = State::OTA_ERROR;
  otaResetTransfer(s);
}

static void startDownload(State& s, const String& fwUrl, const String& md5Need) {
  if (!md5Need.length()) {
    otaFail(s, "MD5 missing");
    return;
  }

  // HTTP GET starten
  gHttp.setTimeout(12000);
  gHttp.setReuse(false);

  if (!gHttp.begin(gClient, fwUrl)) {
    otaFail(s, "http begin failed");
    return;
  }

  int code = gHttp.GET();
  if (code != 200) {
    gHttp.end();
    otaFail(s, String("http ") + code);
    return;
  }

  int total = gHttp.getSize(); // kann -1 sein
  if (total <= 0) {
    // trotzdem weiter, aber Progress nur bytes
    total = 0;
  }

  // MD5 Pflicht
  Update.setMD5(md5Need.c_str());

  bool ok = Update.begin((total > 0) ? (size_t)total : (size_t)0);
  if (!ok) {
    gHttp.end();
    otaFail(s, String("Update.begin failed: ") + Update.getError());
    return;
  }

  s.otaBytesCur = 0;
  s.otaBytesTotal = (uint32_t)total;
  s.otaPct = 0;
  s.otaStage = State::OTA_DOWNLOADING;
  s.otaLastMsg = "downloading...";
  gHttpOpen = true;
  gLastChunkMs = millis();
}

static void stepDownload(State& s) {
  if (!gHttpOpen) {
    otaFail(s, "download not open");
    return;
  }

  WiFiClient* stream = gHttp.getStreamPtr();
  if (!stream) {
    otaFail(s, "stream null");
    return;
  }

  // kleine Häppchen lesen, damit Webserver weiterläuft
  const size_t CHUNK = 1024;
  uint8_t buf[CHUNK];

  // Rate limit minimal (sonst blockt es trotzdem)
  if (millis() - gLastChunkMs < 5) return;
  gLastChunkMs = millis();

  int avail = stream->available();
  if (avail > 0) {
    int toRead = (avail > (int)CHUNK) ? (int)CHUNK : avail;
    int r = stream->readBytes(buf, toRead);
    if (r > 0) {
      size_t w = Update.write(buf, (size_t)r);
      if (w != (size_t)r) {
        otaFail(s, String("Update.write failed: ") + Update.getError());
        return;
      }
      s.otaBytesCur += (uint32_t)r;
      if (s.otaBytesTotal > 0) {
        s.otaPct = (uint8_t)((s.otaBytesCur * 100UL) / s.otaBytesTotal);
      } else {
        // unknown total -> pseudo pct
        s.otaPct = (s.otaBytesCur > 0) ? 1 : 0;
      }
      s.otaLastMsg = "downloading...";
      return;
    }
  }

  // wenn Verbindung zu ist und nichts mehr verfügbar -> fertig
  if (!stream->connected() && stream->available() == 0) {
    bool endOk = Update.end(true); // true = set boot partition
    gHttp.end();
    gHttpOpen = false;

    if (!endOk) {
      otaFail(s, String("Update.end failed: ") + Update.getError());
      return;
    }

    s.otaPct = 100;
    s.otaLastMsg = "update OK (reboot)";
    delay(50);
    ESP.restart();
  }
}

// ---------------- Public API ----------------
void otaHttpInit(State& s) {
  s.fwLocal = FW_VERSION;

  s.fwRemote = "";
  s.fwRemoteBin = "";
  s.fwRemoteMd5 = "";
  s.fwRemoteSize = 0;

  s.otaUpdateAvailable = false;
  s.otaBusy = false;
  s.otaLastMsg = "ota init";
  s.otaLastCheckMs = 0;

  s.otaDoUpdate = false;
  s.otaRequestedVer = "";

  s.otaPct = 0;
  s.otaBytesCur = 0;
  s.otaBytesTotal = 0;

  s.otaItemCount = 0;
  s.otaStage = State::OTA_IDLE;
}

static void checkManifest(State& s) {
  if (millis() - s.otaLastCheckMs < OTA_CHECK_INTERVAL) return;
  s.otaLastCheckMs = millis();

  // manifest.json
  String murl = String(OTA_BASE_URL) + String(OTA_MANIFEST);
  String mjson = httpGet(murl);
  if (!mjson.length()) {
    s.otaLastMsg = "manifest fetch failed";
    return;
  }

  String latest = jsonGetString(mjson, "latest");
  String bin    = jsonGetString(mjson, "bin");
  String md5    = jsonGetString(mjson, "md5");
  uint32_t size = jsonGetU32(mjson, "size");

  if (!latest.length() || !bin.length() || !md5.length()) {
    s.otaLastMsg = "manifest parse failed";
    return;
  }

  s.fwRemote = latest;
  s.fwRemoteBin = bin;
  s.fwRemoteMd5 = md5;
  s.fwRemoteSize = size;

  s.otaUpdateAvailable = versionGreater(s.fwRemote, s.fwLocal);
  s.otaLastMsg = String("manifest ok: ") + s.fwRemote;

  // versions.json (Rollback Dropdown)
  String vurl = String(OTA_BASE_URL) + String(OTA_VERSIONS);
  String vjson = httpGet(vurl);
  if (vjson.length()) {
    parseVersions(s, vjson);
  }
}

void otaHttpRequestUpdate(State& s, const String& version) {
  if (s.otaBusy) return;
  s.otaDoUpdate = true;
  s.otaRequestedVer = version; // "" oder "latest" => latest
}

static void beginOta(State& s) {
  s.otaBusy = true;
  s.otaPct = 0;
  s.otaBytesCur = 0;
  s.otaBytesTotal = 0;
  s.otaStage = State::OTA_PREPARE;

  // sicher manifest einmal frisch wenn leer
  if (!s.fwRemote.length() || !s.fwRemoteBin.length() || !s.fwRemoteMd5.length()) {
    String murl = String(OTA_BASE_URL) + String(OTA_MANIFEST);
    String mjson = httpGet(murl);
    if (!mjson.length()) { otaFail(s, "manifest fetch failed"); return; }
    s.fwRemote = jsonGetString(mjson, "latest");
    s.fwRemoteBin = jsonGetString(mjson, "bin");
    s.fwRemoteMd5 = jsonGetString(mjson, "md5");
    s.fwRemoteSize = jsonGetU32(mjson, "size");
  }

  // Ziel bestimmen
  String req = s.otaRequestedVer;
  req.trim();
  if (req == "latest") req = "";

  String fwUrl;
  String targetMd5;

  if (!req.length()) {
    fwUrl = String(OTA_BASE_URL) + String(OTA_FIRMWARE_DIR) + "/" + s.fwRemoteBin;
    targetMd5 = s.fwRemoteMd5;
  } else {
    State::OtaItem it;
    if (!findRollbackItem(s, req, it)) {
      otaFail(s, "rollback ver not found");
      return;
    }
    fwUrl = String(OTA_BASE_URL) + String(OTA_FIRMWARE_DIR) + "/" + it.bin;
    targetMd5 = it.md5;
  }

  s.otaLastMsg = "starting download...";
  startDownload(s, fwUrl, targetMd5);
}

void otaHttpLoop(State& s) {
  if (WiFi.status() != WL_CONNECTED) return;

  // regelmäßig manifest/versions holen, aber nicht während OTA
  if (!s.otaBusy) {
    checkManifest(s);
  }

  // OTA Trigger
  if (s.otaDoUpdate && !s.otaBusy) {
    s.otaDoUpdate = false;
    beginOta(s);
  }

  // OTA weiterführen (non-blocking)
  if (s.otaBusy && s.otaStage == State::OTA_DOWNLOADING) {
    stepDownload(s);
  }
}

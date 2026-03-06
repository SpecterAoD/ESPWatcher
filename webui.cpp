#include "webui.h"
#include "config.h"
#include "reset_pi.h"
#include "mqtt_mgr.h"
#include "ota_http.h"
#include "version.h"

#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

static ESP8266WebServer srv(80);
static State* gS = nullptr;

static String age(uint32_t lastMs){
  if (lastMs == 0) return "-";
  uint32_t sec = (millis() - lastMs)/1000;
  return String(sec) + "s";
}

static String jsEscape(const String& in) {
  String s = in;
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\n", "\\n");
  s.replace("\r", "");
  return s;
}

static String jsArrayFromSpark(const Spark& sp) {
  String out = "[";
  uint8_t n = sp.filled ? Spark::N : sp.head;
  uint8_t start = sp.filled ? sp.head : 0;
  for (uint8_t i=0;i<n;i++){
    uint8_t idx = (start + i) % Spark::N;
    out += String(sp.v[idx], 2);
    if (i+1<n) out += ",";
  }
  out += "]";
  return out;
}

static float approxCpuPercentFromLoad(float load1) {
  if (CPU_CORES <= 0) return 0;
  float pct = (load1 / (float)CPU_CORES) * 100.0f;
  if (pct < 0) pct = 0;
  if (pct > 400) pct = 400;
  return pct;
}

static String topicVal(const char* t){
  for (size_t i=0;i<TOPIC_COUNT;i++){
    if (gS->topics[i].topic == String(t)) return gS->topics[i].value;
  }
  return "";
}

static float topicFloat(const char* t, float def=0){
  String v = topicVal(t);
  if (!v.length()) return def;
  return v.toFloat();
}

static long topicLong(const char* t, long def=0){
  String v = topicVal(t);
  if (!v.length()) return def;
  return v.toInt();
}

static void handleApi() {
  float cpuTemp = topicFloat("pi/health/temp_cpu", 0);
  float load1   = topicFloat("pi/health/load1", 0);
  float load5   = topicFloat("pi/health/load5", 0);
  float load15  = topicFloat("pi/health/load15", 0);

  long memAvailKb = topicLong("pi/health/mem_avail_kb", 0);
  long memUsedKb  = topicLong("pi/health/mem_used_kb", 0);

  long dataTotalKb = topicLong("pi/storage/data_total_kb", 0);
  long dataAvailKb = topicLong("pi/storage/data_avail_kb", 0);
  long dataUsePct  = topicLong("pi/storage/data_use_percent", -1);

  String throttledHex = topicVal("pi/power/throttled_hex");
  long uv_now  = topicLong("pi/power/undervoltage_now", 0);
  long uv_past = topicLong("pi/power/undervoltage_past", 0);
  long thr_now = topicLong("pi/power/throttled_now", 0);
  long tmp_now = topicLong("pi/power/temp_limit_now", 0);

  long svc_ftl = topicLong("pi/service/pihole_ftl", 0);
  long svc_mos = topicLong("pi/service/mosquitto", 0);

  float memAvailMb = memAvailKb / 1024.0f;
  float memUsedMb  = memUsedKb  / 1024.0f;
  float dataTotalGb = dataTotalKb / (1024.0f*1024.0f);
  float dataAvailGb = dataAvailKb / (1024.0f*1024.0f);

  float cpuPctApprox = approxCpuPercentFromLoad(load1);

  // Temp-Ampel
  int tempLevel = 0;
  if (cpuTemp >= 78.0f) tempLevel = 2;
  else if (cpuTemp >= 70.0f) tempLevel = 1;

  String power = "OK";
  if (uv_now==1 || thr_now==1 || tmp_now==1) power = "CRITICAL";
  else if (uv_past==1) power = "WARN";

  String json = "{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"gw\":\"" + WiFi.gatewayIP().toString() + "\",";
  json += "\"dns\":\"" + WiFi.dnsIP().toString() + "\",";

  json += "\"staUp\":" + String(gS->staUp?1:0) + ",";
  json += "\"apUp\":" + String(gS->apUp?1:0) + ",";

  json += "\"piSsh\":" + String(gS->piSsh?1:0) + ",";
  json += "\"piDns\":" + String(gS->piDns?1:0) + ",";
  json += "\"piHttps\":" + String(gS->piHttps?1:0) + ",";
  json += "\"piMqttPort\":" + String(gS->piMqtt?1:0) + ",";

  json += "\"ntpOk\":" + String(gS->ntpOk?1:0) + ",";
  json += "\"ntpAge\":" + String(gS->ntpLastSyncMs ? (millis()-gS->ntpLastSyncMs)/1000 : 999999) + ",";

  json += "\"piUnix\":" + String(gS->piUnix) + ",";
  json += "\"espUnix\":" + String(gS->espUnix) + ",";
  json += "\"drift\":" + String(gS->driftSec, 1) + ",";
  json += "\"driftLevel\":" + String(gS->driftLevel) + ",";

  json += "\"mqttConnected\":" + String(gS->mqttConnected?1:0) + ",";
  json += "\"mqttLastAge\":\"" + age(gS->mqttLastOkMs) + "\",";

  json += "\"piTime\":\"" + jsEscape(topicVal("pi/meta/time")) + "\",";

  json += "\"cpuTemp\":" + String(cpuTemp,1) + ",";
  json += "\"tempLevel\":" + String(tempLevel) + ",";
  json += "\"cpuPctApprox\":" + String(cpuPctApprox,1) + ",";
  json += "\"load1\":" + String(load1,2) + ",";
  json += "\"load5\":" + String(load5,2) + ",";
  json += "\"load15\":" + String(load15,2) + ",";

  json += "\"memUsedMb\":" + String(memUsedMb,1) + ",";
  json += "\"memAvailMb\":" + String(memAvailMb,1) + ",";

  json += "\"diskUsePct\":" + String(dataUsePct) + ",";
  json += "\"diskTotalGb\":" + String(dataTotalGb,1) + ",";
  json += "\"diskAvailGb\":" + String(dataAvailGb,1) + ",";

  json += "\"powerState\":\"" + power + "\",";
  json += "\"throttledHex\":\"" + jsEscape(throttledHex) + "\",";

  json += "\"svcFtl\":" + String(svc_ftl) + ",";
  json += "\"svcMos\":" + String(svc_mos) + ",";

  json += "\"esplinkHttp\":" + String(gS->esplinkHttp?1:0) + ",";
  json += "\"esplinkSsid\":" + String(gS->esplinkSsidSeen?1:0) + ",";
  json += "\"esplinkIp\":\"" + ESPLINK_IP.toString() + "\",";

  json += "\"fwLocal\":\"" + jsEscape(gS->fwLocal) + "\",";
  json += "\"fwRemote\":\"" + jsEscape(gS->fwRemote) + "\",";
  json += "\"fwRemoteBin\":\"" + jsEscape(gS->fwRemoteBin) + "\",";
  json += "\"fwRemoteMd5\":\"" + jsEscape(gS->fwRemoteMd5) + "\",";
  json += "\"fwRemoteSize\":" + String(gS->fwRemoteSize) + ",";
  json += "\"otaAvail\":" + String(gS->otaUpdateAvailable?1:0) + ",";
  json += "\"otaBusy\":" + String(gS->otaBusy?1:0) + ",";
  json += "\"otaMsg\":\"" + jsEscape(gS->otaLastMsg) + "\",";

  json += "\"otaItems\":[";
  for (uint8_t i=0;i<gS->otaItemCount;i++){
    json += "{";
    json += "\"ver\":\"" + jsEscape(gS->otaItems[i].ver) + "\",";
    json += "\"bin\":\"" + jsEscape(gS->otaItems[i].bin) + "\",";
    json += "\"md5\":\"" + jsEscape(gS->otaItems[i].md5) + "\",";
    json += "\"size\":" + String(gS->otaItems[i].size);
    json += "}";
    if (i+1<gS->otaItemCount) json += ",";
  }
  json += "],";

  json += "\"spCpu\":" + jsArrayFromSpark(gS->sp_cpuTemp);
  json += "}";

  srv.sendHeader("Cache-Control", "no-store");
  srv.send(200, "application/json", json);
}

static void handleResetRun() {
  srv.send(200, "text/plain", "Resetting Pi via RUN...");
  delay(150);
  resetPiPulse(600);
}

static void handleReboot() {
  if (!gS->mqttConnected) { srv.send(503, "text/plain", "MQTT not connected"); return; }
  bool ok = mqttPublish("pi/cmd/reboot", "1", false);
  srv.send(ok ? 200 : 500, "text/plain", ok ? "Reboot command sent" : "Publish failed");
}

static void handleFtlRestart() {
  if (!gS->mqttConnected) { srv.send(503, "text/plain", "MQTT not connected"); return; }
  bool ok = mqttPublish("pi/cmd/ftl_restart", "1", false);
  srv.send(ok ? 200 : 500, "text/plain", ok ? "FTL restart command sent" : "Publish failed");
}

static void handleOtaUpdate() {
  String ver = srv.arg("plain");
  ver.trim();
  otaHttpRequestUpdate(*gS, ver); // "" => latest
  srv.send(200, "text/plain", "OK");
}

// ---------- HTML in PROGMEM (kein Heap-Geballer) ----------
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>Rescue Monitor</title>
<style>
:root{--bg:#fff;--fg:#111;--mut:#666;--card:#fff;--br:#e6e6e6;--sh:0 6px 18px rgba(0,0,0,.06);}
body.dark{--bg:#0f1115;--fg:#eaecef;--mut:#9aa0a6;--card:#141821;--br:#273043;--sh:0 10px 30px rgba(0,0,0,.35);}
body{margin:14px;font-family:Arial;background:var(--bg);color:var(--fg);}
.top{position:sticky;top:0;background:var(--bg);padding:10px 0 8px 0;z-index:5}
.hrow{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap}
.title{font-size:20px;font-weight:900}
.btn{border:1px solid var(--br);background:var(--card);color:var(--fg);border-radius:12px;padding:8px 10px;font-weight:800}
.tabs{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
.tab{border:1px solid var(--br);background:var(--card);color:var(--fg);border-radius:999px;padding:7px 10px;font-weight:900;cursor:pointer}
.tab.active{outline:2px solid #1e8e3e}
.section{display:none}.section.show{display:block}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}
@media(min-width:900px){.grid{grid-template-columns:repeat(4,minmax(0,1fr));}}
.card{background:var(--card);border:1px solid var(--br);border-radius:16px;padding:12px 14px;box-shadow:var(--sh)}
.k{color:var(--mut);font-size:12px;font-weight:800}
.v{font-size:22px;font-weight:900;margin-top:2px}
.sub{color:var(--mut);font-size:12px;margin-top:6px;line-height:1.35}
.pill{display:inline-flex;align-items:center;gap:8px;padding:6px 10px;border-radius:999px;font-weight:900;border:1px solid var(--br);background:var(--card)}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block;background:#f9ab00}
.row{display:flex;gap:10px;flex-wrap:wrap;margin-top:10px}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,"Liberation Mono","Courier New",monospace}
.cmd{display:block;width:100%;text-align:left;padding:10px 12px;border:1px solid var(--br);border-radius:12px;background:var(--card);color:var(--fg);font-weight:800;cursor:pointer}
.cmd small{display:block;color:var(--mut);font-weight:700;margin-top:5px}
.badge{display:inline-flex;align-items:center;gap:8px;padding:6px 10px;border-radius:12px;border:1px solid var(--br);background:var(--card);font-weight:900}
.spin{display:inline-block;width:12px;height:12px;border:2px solid var(--br);border-top-color:#1e8e3e;border-radius:50%;animation:sp 1s linear infinite}
@keyframes sp{to{transform:rotate(360deg)}}
.reset{width:100%;padding:14px 14px;font-size:18px;border:0;border-radius:14px;background:#111;color:#fff;font-weight:900}
body.dark .reset{background:#eaecef;color:#111}
</style>
</head><body>

<div class="top">
  <div class="hrow">
    <div>
      <div class="title">Rescue Monitor</div>
      <div class="sub">ESP IP: <span class="mono" id="meip">-</span> | Build: <span class="mono" id="build">-</span></div>
    </div>
    <button class="btn" id="dm">Dark Mode</button>
  </div>

  <div class="row" style="margin-top:10px">
    <span class="pill"><span class="dot" id="dotWifi"></span><span id="wifiTxt">WiFi</span></span>
    <span class="pill"><span class="dot" id="dotMqtt"></span><span id="mqttTxt">MQTT</span></span>
    <span class="pill"><span class="dot" id="dotPower"></span><span id="powerTxt">Power</span></span>
    <span class="pill"><span class="dot" id="dotSvc"></span><span id="svcTxt">Services</span></span>
    <span class="pill"><span class="dot" id="dotTemp"></span><span id="tempTxt">Temp</span></span>
    <span class="pill"><span class="dot" id="dotEspl"></span><span id="esplTxt">ESP-link</span></span>
    <span class="pill"><span class="dot" id="dotNtp"></span><span id="ntpTxt">NTP</span></span>
    <span class="pill"><span class="dot" id="dotDrift"></span><span id="driftTxt">Drift</span></span>
  </div>

  <div class="tabs">
    <button class="tab active" data-tab="t_overview">Overview</button>
    <button class="tab" data-tab="t_ota">OTA</button>
    <button class="tab" data-tab="t_actions">Actions</button>
    <button class="tab" data-tab="t_cmds">Commands</button>
    <button class="tab" data-tab="t_raw">Raw</button>
  </div>
</div>

<div class="section show" id="t_overview">
  <div class="grid">
    <div class="card"><div class="k">CPU Temp</div><div class="v" id="cpuTemp">-</div><div class="sub">CPU approx: <span id="cpuPct">-</span>% | load1 <span id="l1">-</span></div></div>
    <div class="card"><div class="k">RAM</div><div class="v" id="ram">-</div><div class="sub">used / avail</div></div>
    <div class="card"><div class="k">Storage</div><div class="v" id="disk">-</div><div class="sub" id="diskSub">-</div></div>
    <div class="card"><div class="k">Pi time</div><div class="v" style="font-size:16px;" id="pitime">-</div><div class="sub">Last MQTT msg: <span id="lastMsg">-</span></div></div>
    <div class="card"><div class="k">Connectivity</div><div class="v" style="font-size:16px;" id="net">-</div><div class="sub">GW <span id="gw">-</span> | DNS <span id="dns">-</span></div></div>
    <div class="card"><div class="k">Ports</div><div class="v" style="font-size:16px;" id="ports">-</div><div class="sub">Pi</div></div>
  </div>
</div>

<div class="section" id="t_ota">
  <div class="card">
    <div class="k">Firmware</div>
    <div class="v" style="font-size:18px" id="fwLine">Local ? | Remote ?</div>
    <div class="sub mono" id="fwMeta">bin: - | md5: - | size: -</div>
    <div class="row" style="margin-top:12px">
      <button class="btn" id="btnUpdLatest">Update (latest)</button>
      <span class="badge"><span id="otaSpin" style="display:none" class="spin"></span><span id="otaMsg">-</span></span>
    </div>
    <div class="row" style="margin-top:12px">
      <select class="btn" id="rbSel"></select>
      <button class="btn" id="btnRollback">Rollback</button>
    </div>
    <div class="sub">Hinweis: Beim Flashen blockiert der ESP kurz. Eine Live-Prozessbar ist daher unzuverlässig – Status kommt nach Reboot.</div>
  </div>
</div>

<div class="section" id="t_actions">
  <div class="card">
    <div class="k">Actions</div>
    <button class="reset" id="resetBtn">Hold 2s to RESET PI (RUN)</button>
    <div style="height:10px"></div>
    <button class="reset" id="rebootBtn">Hold 2s to REBOOT PI (MQTT)</button>
    <div style="height:10px"></div>
    <button class="reset" id="ftlBtn">Hold 2s to RESTART FTL (MQTT)</button>
    <div class="sub">Mit ESP-link bleiben diese Actions sinnvoll (Pi reboot / Pi-hole FTL restart / RUN Reset).</div>
  </div>
</div>

<div class="section" id="t_cmds">
  <div class="card">
    <div class="k">Schnellzugriff</div>
    <div class="row">
      <a class="btn" href="http://192.168.2.16/" target="_blank">ESP-link öffnen (192.168.2.16)</a>
      <button class="btn" id="btnPiReboot">Pi reboot</button>
      <button class="btn" id="btnFtlRestart">FTL restart</button>
      <button class="btn" id="btnPiResetRun">Pi RESET (RUN)</button>
    </div>
    <div class="sub">Tippe einen Befehl zum Kopieren:</div>

    <div class="k" style="margin-top:10px">Basics</div>
    <div class="cmd mono" data-copy="ssh piroot@192.168.2.6">ssh piroot@192.168.2.6</div>
    <div class="cmd mono" data-copy="uname -a">uname -a</div>
    <div class="cmd mono" data-copy="uptime">uptime</div>
    <div class="cmd mono" data-copy="date --iso-8601=seconds && date +%s">date --iso-8601=seconds && date +%s</div>

    <div class="k" style="margin-top:10px">Health</div>
    <div class="cmd mono" data-copy="vcgencmd measure_temp && vcgencmd get_throttled">vcgencmd measure_temp && vcgencmd get_throttled</div>
    <div class="cmd mono" data-copy="free -h">free -h</div>
    <div class="cmd mono" data-copy="df -h / /mnt/data">df -h / /mnt/data</div>
    <div class="cmd mono" data-copy="top -b -n1 | head -n 20">top -b -n1 | head -n 20</div>

    <div class="k" style="margin-top:10px">systemd</div>
    <div class="cmd mono" data-copy="systemctl --no-pager --failed">systemctl --no-pager --failed</div>
    <div class="cmd mono" data-copy="systemctl --no-pager status pihole-FTL mosquitto ssh ufw">systemctl status pihole-FTL mosquitto ssh ufw</div>
    <div class="cmd mono" data-copy="journalctl -u pihole-FTL -n 50 --no-pager">journalctl -u pihole-FTL -n 50</div>
    <div class="cmd mono" data-copy="journalctl -u mosquitto -n 50 --no-pager">journalctl -u mosquitto -n 50</div>

    <div class="k" style="margin-top:10px">Pi-hole</div>
    <div class="cmd mono" data-copy="pihole status">pihole status</div>
    <div class="cmd mono" data-copy="pihole -v">pihole -v</div>
    <div class="cmd mono" data-copy="pihole -t">pihole -t</div>

    <div class="k" style="margin-top:10px">MQTT</div>
    <div class="cmd mono" data-copy="mosquitto_sub -h 127.0.0.1 -p 1883 -u espmon -P esp8266 -t &quot;pi/#&quot; -v">mosquitto_sub pi/#</div>

    <div class="k" style="margin-top:10px">Firewall / UFW</div>
    <div class="cmd mono" data-copy="sudo ufw status verbose">sudo ufw status verbose</div>
    <div class="cmd mono" data-copy="sudo ss -tulpn">sudo ss -tulpn</div>

    <div class="k" style="margin-top:10px">Updates</div>
    <div class="cmd mono" data-copy="sudo apt update">sudo apt update</div>
    <div class="cmd mono" data-copy="apt list --upgradable">apt list --upgradable</div>
    <div class="cmd mono" data-copy="sudo apt upgrade -y">sudo apt upgrade -y</div>

    <div class="k" style="margin-top:10px">OTA Server</div>
    <div class="cmd mono" data-copy="curl -v http://192.168.2.6:8090/firmwares/manifest.json">curl manifest.json</div>

    <div class="sub" id="toast" style="margin-top:10px"></div>
  </div>
</div>

<div class="section" id="t_raw">
  <div class="card"><div class="k">Raw JSON</div><pre id="raw" style="white-space:pre-wrap;margin-top:10px;color:var(--mut)">loading...</pre></div>
</div>

<script>
const $=id=>document.getElementById(id);
const setDot=(id,color)=>{const e=$(id); if(e) e.style.background=color;};
const fmt=(n,d=1)=>Number(n).toFixed(d);

function toast(msg){
  const el=$('toast'); if(!el) return;
  el.textContent=msg;
  setTimeout(()=>{ if(el.textContent===msg) el.textContent=''; }, 1800);
}
async function post(url, body=''){
  try{
    const r=await fetch(url,{method:'POST',body:body});
    const t=await r.text();
    toast((r.ok?'OK: ':'ERR: ')+t);
  }catch(e){ toast('ERR: request failed'); }
}

function longPress(btn, action, idleText){
  let t=null;
  const down=()=>{btn.textContent='Holding...'; t=setTimeout(()=>{action();},2000);};
  const up=()=>{if(t){clearTimeout(t);t=null;} btn.textContent=idleText;};
  btn.onmousedown=down; btn.onmouseup=up; btn.onmouseleave=up;
  btn.ontouchstart=(e)=>{e.preventDefault(); down();};
  btn.ontouchend=(e)=>{e.preventDefault(); up();};
}

document.addEventListener('DOMContentLoaded',()=>{
  $('meip').textContent=location.host;
  const dm=localStorage.getItem('dm')==='1'; if(dm) document.body.classList.add('dark');
  $('dm').onclick=()=>{document.body.classList.toggle('dark'); localStorage.setItem('dm',document.body.classList.contains('dark')?'1':'0');};

  document.querySelectorAll('.tab').forEach(b=>b.addEventListener('click',()=>{
    document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active')); b.classList.add('active');
    const id=b.getAttribute('data-tab');
    document.querySelectorAll('.section').forEach(s=>s.classList.remove('show'));
    const sec=document.getElementById(id); if(sec) sec.classList.add('show');
  }));

  longPress($('resetBtn'), ()=>post('/reset'), 'Hold 2s to RESET PI (RUN)');
  longPress($('rebootBtn'), ()=>post('/reboot'), 'Hold 2s to REBOOT PI (MQTT)');
  longPress($('ftlBtn'), ()=>post('/ftlrestart'), 'Hold 2s to RESTART FTL (MQTT)');

  const br=$('btnPiReboot'); if(br) br.onclick=()=>post('/reboot');
  const bf=$('btnFtlRestart'); if(bf) bf.onclick=()=>post('/ftlrestart');
  const brr=$('btnPiResetRun'); if(brr) brr.onclick=()=>post('/reset');

  function copyText(txt){
    if(navigator.clipboard && navigator.clipboard.writeText) return navigator.clipboard.writeText(txt);
    const ta=document.createElement('textarea'); ta.value=txt; document.body.appendChild(ta); ta.select(); document.execCommand('copy'); document.body.removeChild(ta);
    return Promise.resolve();
  }
  document.querySelectorAll('[data-copy]').forEach(b=>b.addEventListener('click',()=>{
    const txt=b.getAttribute('data-copy')||'';
    copyText(txt).then(()=>toast('Copied')).catch(()=>toast('Copy failed'));
  }));

  $('btnUpdLatest').onclick=()=>post('/api/ota/update','');
  $('btnRollback').onclick=()=>post('/api/ota/update', ($('rbSel').value||''));

  tick(); setInterval(tick, 2500);
});

async function tick(){
  try{
    const r=await fetch('/api/state',{cache:'no-store'});
    const s=await r.json();

    $('build').textContent='FW '+(s.fwLocal||'?');

    setDot('dotWifi', s.staUp? '#1e8e3e':'#d93025');
    setDot('dotMqtt', s.mqttConnected? '#1e8e3e':'#f9ab00');
    setDot('dotPower', s.powerState==='CRITICAL' ? '#d93025' : (s.powerState==='WARN'?'#f9ab00':'#1e8e3e'));
    setDot('dotSvc', (s.svcFtl==1 && s.svcMos==1)?'#1e8e3e':'#f9ab00');
    setDot('dotEspl', s.esplinkHttp ? '#1e8e3e' : (s.esplinkSsid ? '#f9ab00' : '#d93025'));
    setDot('dotNtp', s.ntpOk ? '#1e8e3e' : '#d93025');

    const tcol=(s.tempLevel==2)?'#d93025':((s.tempLevel==1)?'#f9ab00':'#1e8e3e');
    setDot('dotTemp', tcol);

    const dcol=(s.driftLevel==2)?'#d93025':((s.driftLevel==1)?'#f9ab00':'#1e8e3e');
    setDot('dotDrift', dcol);

    $('wifiTxt').textContent='WiFi '+(s.staUp?'OK':'DOWN')+' | '+s.ip+' | '+s.rssi+' dBm';
    $('mqttTxt').textContent='MQTT '+(s.mqttConnected?'OK':'...')+' | last '+s.mqttLastAge;
    $('powerTxt').textContent='Power '+s.powerState+' | '+(s.throttledHex||'');
    $('svcTxt').textContent='FTL '+(s.svcFtl==1?'OK':'DOWN')+' | Mosq '+(s.svcMos==1?'OK':'DOWN');
    $('tempTxt').textContent='Temp '+(s.cpuTemp?fmt(s.cpuTemp,1)+'°C':'-');
    $('esplTxt').textContent='ESP-link '+(s.esplinkHttp?'OK':'...')+' | '+(s.esplinkHttp?(s.esplinkIp+':80'):(s.esplinkSsid?'SSID seen':'DOWN'));
    $('ntpTxt').textContent=s.ntpOk?('NTP OK | age '+s.ntpAge+'s'):'NTP FAIL';
    $('driftTxt').textContent='Drift '+s.drift+' s';

    $('cpuTemp').textContent=(s.cpuTemp?fmt(s.cpuTemp,1)+'°C':'-');
    $('cpuPct').textContent=fmt(s.cpuPctApprox,1);
    $('l1').textContent=fmt(s.load1,2);
    $('ram').textContent=fmt(s.memUsedMb,1)+' / '+fmt(s.memAvailMb,1)+' MB';
    $('disk').textContent=s.diskUsePct+' %';
    $('diskSub').textContent=fmt(s.diskAvailGb,1)+' GB free / '+fmt(s.diskTotalGb,1)+' GB total';
    $('pitime').textContent=s.piTime||'-';
    $('lastMsg').textContent=s.mqttLastAge;

    $('net').textContent=s.ip+' ('+s.rssi+' dBm)';
    $('gw').textContent=s.gw; $('dns').textContent=s.dns;
    $('ports').textContent='22 '+(s.piSsh?'OK':'FAIL')+' | 53 '+(s.piDns?'OK':'FAIL')+' | 443 '+(s.piHttps?'OK':'FAIL')+' | 1883 '+(s.piMqttPort?'OK':'FAIL');

    $('fwLine').textContent='Local '+(s.fwLocal||'?')+' | Remote '+(s.fwRemote||'?')+(s.otaAvail?' (UPDATE!)':'');
    $('fwMeta').textContent='bin: '+(s.fwRemoteBin||'-')+' | md5: '+(s.fwRemoteMd5||'-')+' | size: '+(s.fwRemoteSize||0);
    $('otaSpin').style.display=s.otaBusy?'inline-block':'none';
    $('otaMsg').textContent=(s.otaMsg||'-');
    $('btnUpdLatest').disabled=!!s.otaBusy;
    $('btnRollback').disabled=!!s.otaBusy;

    const sel=$('rbSel');
    if(sel && Array.isArray(s.otaItems)){
      const cur=sel.value;
      sel.innerHTML='';
      const opt0=document.createElement('option');
      opt0.value=''; opt0.textContent='Select rollback version';
      sel.appendChild(opt0);

      const items=[...s.otaItems].sort((a,b)=>{
        const pa=(a.ver||'0').split('.').map(x=>parseInt(x||'0',10));
        const pb=(b.ver||'0').split('.').map(x=>parseInt(x||'0',10));
        for(let i=0;i<4;i++){const va=pa[i]||0, vb=pb[i]||0; if(va!==vb) return vb-va;}
        return 0;
      });
      items.forEach(it=>{
        const o=document.createElement('option');
        o.value=it.ver;
        o.textContent=it.ver+' | '+(it.size||0)+' bytes | '+(it.md5||'');
        sel.appendChild(o);
      });
      if(cur) sel.value=cur;
    }

    $('raw').textContent=JSON.stringify(s,null,2);
  }catch(e){}
}
</script>
</body></html>
)HTML";

static void handleRoot() {
  srv.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  srv.sendHeader("Pragma", "no-cache");
  srv.sendHeader("Expires", "0");
  srv.send(200, "text/html; charset=utf-8", FPSTR(INDEX_HTML));
}

void webInit(State& s) {
  gS = &s;
  srv.on("/", HTTP_GET, handleRoot);
  srv.on("/api/state", HTTP_GET, handleApi);
  srv.on("/api/ota/update", HTTP_POST, handleOtaUpdate);
  srv.on("/reset", HTTP_POST, handleResetRun);
  srv.on("/reboot", HTTP_POST, handleReboot);
  srv.on("/ftlrestart", HTTP_POST, handleFtlRestart);
  srv.begin();
}

void webLoop() {
  srv.handleClient();
}

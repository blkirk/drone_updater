#include "web_ui.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "logger.h"
#include "status.h"
#include "storage.h"

namespace web_ui {

namespace {

WebServer s_server(80);
bool      s_running   = false;
bool      s_routes_ok = false;
Command   s_cmd       = Command::kNone;

// Upload state (WebServer streams the body in chunks).
File   s_up_file;
bool   s_up_failed = false;
size_t s_up_bytes  = 0;
String s_up_name;

const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Drone DFU</title>
<style>
body{font-family:system-ui,sans-serif;margin:0 auto;max-width:640px;padding:12px;background:#111;color:#ddd}
h1{font-size:1.3em}h2{font-size:1.05em;margin:18px 0 6px}
section{background:#1c1c1c;border-radius:8px;padding:10px 12px;margin-bottom:10px}
button{background:#2d6cdf;color:#fff;border:0;border-radius:6px;padding:8px 14px;margin:4px 4px 4px 0;font-size:1em}
button.warn{background:#a33}button.alt{background:#444}
textarea,pre{width:100%;box-sizing:border-box;background:#000;color:#cfc;font:12px monospace;border-radius:6px}
textarea{height:220px;padding:6px}pre{height:260px;overflow:auto;padding:6px;white-space:pre-wrap}
.bar{height:14px;background:#333;border-radius:7px;overflow:hidden}.bar>div{height:100%;background:#3c3;width:0}
#msg{min-height:1.2em;color:#fc6}
</style></head><body>
<h1>Drone DFU updater</h1>
<section><h2>Status</h2>
<div id="st">…</div><div class="bar"><div id="pb"></div></div>
<button onclick="post('/start')">Start now</button><button class="alt" onclick="post('/cancel')">Cancel countdown</button>
</section>
<section><h2>Firmware (.zip from adafruit-nrfutil dfu genpkg)</h2>
<div id="fw"></div>
<input type="file" id="file" accept=".zip"> <button onclick="upload()">Upload</button>
<div id="msg"></div></section>
<section><h2>Config</h2>
<textarea id="cfg" spellcheck="false"></textarea>
<button onclick="saveCfg()">Save</button><button class="alt" onclick="loadCfg()">Reload</button>
<button class="warn" onclick="if(confirm('Reboot?'))post('/reboot')">Reboot</button></section>
<section><h2>Log</h2><pre id="log"></pre>
<button class="alt" onclick="loadLog()">Refresh</button><a href="/log" download="log.txt"><button class="alt">Download</button></a>
<button class="warn" onclick="if(confirm('Clear log?'))post('/log/clear').then(loadLog)">Clear</button></section>
<script>
const $=id=>document.getElementById(id);
function msg(t){$('msg').textContent=t}
async function post(u,body){const r=await fetch(u,{method:'POST',body});const t=await r.text();if(!r.ok)msg(t);return t}
async function status(){try{const s=await (await fetch('/status')).json();
let t=s.state;if(s.state=='countdown')t+=' — starting in '+s.countdown+' s';
if(s.state=='streaming')t+=' '+s.pct+'%';if(s.result)t+=' | last: '+s.result;
$('st').textContent=t;$('pb').style.width=(s.state=='streaming'||s.state=='done_ok'?s.pct:0)+'%';
$('fw').textContent=s.fw?('Stored: '+s.fw_name+' ('+s.fw_size+' B)'):'No firmware stored';
$('fw').textContent+=' — free '+Math.round(s.free/1024)+' KB';}catch(e){$('st').textContent='(no connection)'}}
async function upload(){const f=$('file').files[0];if(!f){msg('Pick a .zip first');return}
const fd=new FormData();fd.append('fw',f,f.name);msg('Uploading…');
const r=await fetch('/upload',{method:'POST',body:fd});msg(await r.text());status()}
async function loadCfg(){$('cfg').value=await (await fetch('/config')).text()}
async function saveCfg(){const r=await fetch('/config',{method:'POST',body:$('cfg').value});msg(await r.text())}
async function loadLog(){const p=$('log');p.textContent=await (await fetch('/log')).text();p.scrollTop=p.scrollHeight}
status();loadCfg();loadLog();setInterval(status,1000);
</script></body></html>
)HTML";

void send_text(int code, const String& s) { s_server.send(code, "text/plain", s); }

void handle_status() {
  JsonDocument d;
  size_t fw_size = 0;
  bool   fw      = storage::fw_present(&fw_size);
  d["state"]     = status::phase_name(status::phase);
  d["countdown"] = (uint32_t)status::countdown_s;
  d["pct"]       = (uint8_t)status::pct;
  d["result"]    = status::last_result;
  d["fw"]        = fw;
  d["fw_size"]   = fw_size;
  if (fw) d["fw_name"] = storage::fw_name();
  d["free"]      = LittleFS.totalBytes() - LittleFS.usedBytes();
  String out;
  serializeJson(d, out);
  s_server.send(200, "application/json", out);
}

// Streaming body handler for POST /upload.
void handle_upload_chunk() {
  HTTPUpload& up = s_server.upload();
  switch (up.status) {
    case UPLOAD_FILE_START:
      LittleFS.remove(storage::kFwTmpPath);
      s_up_file   = LittleFS.open(storage::kFwTmpPath, FILE_WRITE);
      s_up_failed = !s_up_file;
      s_up_bytes  = 0;
      s_up_name   = up.filename;
      break;
    case UPLOAD_FILE_WRITE:
      if (!s_up_failed) {
        if (s_up_file.write(up.buf, up.currentSize) != up.currentSize) s_up_failed = true;
        s_up_bytes += up.currentSize;
      }
      break;
    case UPLOAD_FILE_END:
      if (s_up_file) s_up_file.close();
      break;
    case UPLOAD_FILE_ABORTED:
      if (s_up_file) s_up_file.close();
      s_up_failed = true;
      break;
  }
}

// Completion handler for POST /upload: validate, then promote to /fw.zip.
void handle_upload_done() {
  if (s_up_failed || s_up_bytes == 0) {
    LittleFS.remove(storage::kFwTmpPath);
    send_text(400, s_up_bytes ? "Upload failed (filesystem full?)" : "Empty upload");
    return;
  }
  firmware_zip::Parsed p;
  char err[96];
  if (!storage::validate_zip(storage::kFwTmpPath, &p, err, sizeof(err))) {
    LittleFS.remove(storage::kFwTmpPath);
    logger::log("web: rejected upload '%s': %s", s_up_name.c_str(), err);
    send_text(400, String("Rejected: ") + err);
    return;
  }
  storage::delete_fw();
  if (!LittleFS.rename(storage::kFwTmpPath, storage::kFwPath)) {
    send_text(500, "rename failed");
    return;
  }
  File n = LittleFS.open(storage::kFwNamePath, FILE_WRITE);
  if (n) { n.print(s_up_name); n.close(); }
  logger::log("web: stored '%s' (%u B, app %lu B, dat %lu B)", s_up_name.c_str(),
              (unsigned)s_up_bytes, (unsigned long)p.bin.size, (unsigned long)p.dat.size);
  s_cmd = Command::kRestartCountdown;
  send_text(200, "OK: " + s_up_name + " stored (app " + String(p.bin.size) + " B)");
}

void handle_config_get() {
  String text;
  if (!storage::read_config_text(&text)) {
    char buf[512];
    config::serialize(config::current(), buf, sizeof(buf));
    text = String("# defaults (no /config.txt saved yet)\n") + buf;
  }
  send_text(200, text);
}

void handle_config_post() {
  String body = s_server.arg("plain");
  if (body.length() > 4096) { send_text(400, "config too large"); return; }
  if (!storage::save_config(body.c_str(), body.length())) {
    send_text(500, "save failed");
    return;
  }
  logger::log("web: config saved");
  s_cmd = Command::kRestartCountdown;
  send_text(200, "Saved. WiFi changes apply after reboot.");
}

void handle_log() {
  if (!storage::exists(storage::kLogPath)) { send_text(200, ""); return; }
  File f = LittleFS.open(storage::kLogPath, FILE_READ);
  if (!f) { send_text(200, ""); return; }
  s_server.streamFile(f, "text/plain");
  f.close();
}

void setup_routes() {
  s_server.on("/", HTTP_GET, [] { s_server.send_P(200, "text/html", kIndexHtml); });
  s_server.on("/status", HTTP_GET, handle_status);
  s_server.on("/upload", HTTP_POST, handle_upload_done, handle_upload_chunk);
  s_server.on("/config", HTTP_GET, handle_config_get);
  s_server.on("/config", HTTP_POST, handle_config_post);
  s_server.on("/log", HTTP_GET, handle_log);
  s_server.on("/log/clear", HTTP_POST, [] { logger::clear(); send_text(200, "cleared"); });
  s_server.on("/start", HTTP_POST, [] { s_cmd = Command::kStart; send_text(200, "starting"); });
  s_server.on("/cancel", HTTP_POST, [] { s_cmd = Command::kCancel; send_text(200, "cancelled"); });
  s_server.on("/reboot", HTTP_POST, [] { s_cmd = Command::kReboot; send_text(200, "rebooting"); });
  s_server.onNotFound([] { send_text(404, "not found"); });
}

}  // namespace

void start() {
  if (s_running) return;
  const config::Config& cfg = config::current();

  char ssid[33];
  if (cfg.wifi_ssid[0]) {
    snprintf(ssid, sizeof(ssid), "%s", cfg.wifi_ssid);
  } else {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(ssid, sizeof(ssid), "DroneDFU-%02X%02X", mac[4], mac[5]);
  }
  // WPA2 needs >= 8 chars; anything shorter means an open AP.
  const char* pass = strlen(cfg.wifi_pass) >= 8 ? cfg.wifi_pass : nullptr;

  logger::log("wifi: START access point '%s'...", ssid);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, pass)) {
    logger::log("wifi: START FAILED (softAP)");
    return;
  }
  if (!s_routes_ok) { setup_routes(); s_routes_ok = true; }
  s_server.begin();
  s_running = true;
  logger::log("wifi: STARTED AP '%s' (%s, ch %d) web UI at http://%s/", ssid,
              pass ? "WPA2" : "open", WiFi.channel(), WiFi.softAPIP().toString().c_str());
}

void stop() {
  if (!s_running) return;
  logger::log("wifi: STOP (clients=%d)", WiFi.softAPgetStationNum());
  s_server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  s_running = false;
  logger::log("wifi: STOPPED, radio free for BLE");
}

bool running() { return s_running; }

void handle() {
  if (s_running) s_server.handleClient();
}

Command take_command() {
  Command c = s_cmd;
  s_cmd = Command::kNone;
  return c;
}

}  // namespace web_ui

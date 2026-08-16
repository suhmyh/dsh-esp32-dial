// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// Provisioning portal implementation. See Provision.h for the design rationale.

#include "Provision.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_efuse.h>
#include <mbedtls/md.h>

#include "Config.h"
#include "Settings.h"

Provisioner provisioner;

namespace {
DNSServer dnsServer;
WebServer httpServer(80);

constexpr const char* kHtmlForm = R"(
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>DSH Dial 配置</title>
<style>
  body{font-family:-apple-system,sans-serif;background:#0D111D;color:#fff;margin:0;padding:20px;max-width:500px}
  h1{margin:0 0 4px;color:#1E88E5;font-size:20px}
  p{margin:0 0 20px;color:#888;font-size:13px}
  label{display:block;margin:12px 0 4px;font-size:13px;font-weight:600}
  select,input[type=text],input[type=password]{width:100%;padding:10px;border:1px solid #333;border-radius:6px;background:#1a1a2e;color:#fff;font-size:14px;box-sizing:border-box}
  select option{background:#1a1a2e}
  .hint{font-size:11px;color:#666;margin:4px 0 0}
  button{width:100%;margin-top:24px;padding:12px;border:none;border-radius:6px;background:#1E88E5;color:#fff;font-size:16px;font-weight:600;cursor:pointer}
  button:active{background:#1565C0}
  .msg{margin-top:16px;padding:10px;border-radius:6px;display:none}
  .msg.ok{background:#1b5e20;display:block}
  .msg.err{background:#b71c1c;display:block}
  .row{display:flex;gap:8px}
  .row input{flex:1}
  .row input:first-child{flex:3}
  .btn-sm{padding:10px;border:1px solid #333;border-radius:6px;background:#1a1a2e;color:#fff;cursor:pointer;font-size:13px;white-space:nowrap}
  @media(prefers-color-scheme:light){body{background:#f5f5f5;color:#111}select,input[type=text],input[type=password]{background:#fff;color:#111;border-color:#ccc}select option{background:#fff}}
</style>
</head>
<body>
<h1>DSH 圆盘配置</h1>
<p id="reason"></p>
<form id="form" onsubmit="return save(event)">
<label for="ssid">WiFi 网络</label>
<div class="row">
  <input type="text" id="ssid" placeholder="SSID" required>
  <button type="button" class="btn-sm" onclick="scan()">扫描</button>
</div>
<div class="hint">键入网络名，或点击"扫描"选择附近网络</div>
<div id="networks" style="display:none">
  <select id="netlist" size="6" onchange="document.getElementById('ssid').value=this.value"></select>
</div>
<label for="pass">WiFi 密码</label>
<input type="password" id="pass" placeholder="(留空=开放网络)">
<label for="host">桥接地址</label>
<input type="text" id="host" placeholder="192.168.1.20" required>
<label for="port">端口</label>
<input type="text" id="port" value="3082" inputmode="numeric">
<label for="token">设备令牌</label>
<input type="password" id="token" placeholder="从桥接日志复制" required>
<div class="hint">在电脑上运行 bridge.js，首次启动会打印 token</div>
<button type="submit">保存并重启</button>
</form>
<div id="result" class="msg"></div>
<script>
let scanning=false;
async function scan(){
  if(scanning)return;
  scanning=true;
  const btn=document.querySelector('.btn-sm');
  btn.textContent='扫描中…';
  document.getElementById('networks').style.display='none';
  try{
    const r=await fetch('/scan');
    const list=await r.json();
    const sel=document.getElementById('netlist');
    sel.innerHTML=list.map(s=>'<option>'+escape(s)+'</option>').join('');
    if(list.length)document.getElementById('networks').style.display='block';
  }catch(e){}
  btn.textContent='扫描';
  scanning=false;
}
function escape(s){return s.replace(/[<>&"']/g,function(c){return{'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;',"'":'&#39;'}[c];})}
async function save(ev){
  ev.preventDefault();
  const b=new URLSearchParams({ssid:document.getElementById('ssid').value,pass:document.getElementById('pass').value,host:document.getElementById('host').value,port:document.getElementById('port').value,token:document.getElementById('token').value});
  const r=await fetch('/save',{method:'POST',body:b});
  const j=await r.json();
  const el=document.getElementById('result');
  el.className='msg '+(j.ok?'ok':'err');
  el.textContent=j.ok?'配置已保存，设备正在重启…':'保存失败: '+j.error;
  if(j.ok)setTimeout(()=>{},1500);
}
</script>
</body>
</html>
)";
}  // namespace

// ── credential derivation ──────────────────────────────────────────────────

/**
 * Derive the AP name and password from the chip's factory MAC.
 *
 * Per-device and stable across reboots, so the label printed on the dial keeps
 * working and two dials on one desk do not collide. The password matters: an
 * open AP would let any passer-by post a bridge address — and therefore a
 * destination for approval answers — to the device.
 */
void Provisioner::deriveCredentials() {
  uint8_t mac[6] = {0};
  // Read the factory MAC directly: WiFi.macAddress(uint8_t*) needs the radio
  // to be up, while esp_efuse_mac_get_default() works before WiFi.begin().
  esp_efuse_mac_get_default(mac);

  char buf[32];
  snprintf(buf, sizeof(buf), "%s-%02X%02X", DSH_AP_SSID_PREFIX, mac[4], mac[5]);
  apSsid_ = String(buf);

  // Password: hex digits from SHA256(MAC) — not the MAC itself, which is
  // visible in every beacon frame the AP sends.
  uint8_t hash[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, mac, sizeof(mac));
  mbedtls_md_finish(&ctx, hash);
  mbedtls_md_free(&ctx);

  static_assert(DSH_AP_PASSWORD_LEN >= 8,
                "WPA2 requires at least 8 characters");
  for (int i = 0; i < DSH_AP_PASSWORD_LEN; ++i) {
    buf[i] = "0123456789abcdef"[hash[i] & 0x0F];
  }
  buf[DSH_AP_PASSWORD_LEN] = '\0';
  apPassword_ = String(buf);
}

// ── HTTP handlers ──────────────────────────────────────────────────────────

void Provisioner::handleRoot() {
  httpServer.send(200, "text/html; charset=utf-8", kHtmlForm);
}

void Provisioner::handleNotFound() {
  // Captive portal: every URL serves the same form. iOS/Android probe
  // captive.apple.com / connectivitycheck.gstatic.com; this answers all of
  // them with the HTML page, which the OS then shows as the "login page".
  handleRoot();
}

void Provisioner::handleSave() {
  const String ssid = httpServer.arg("ssid");
  const String pass = httpServer.arg("pass");
  const String host = httpServer.arg("host");
  const String portStr = httpServer.arg("port");
  const String token = httpServer.arg("token");

  if (ssid.length() == 0 || host.length() == 0 || token.length() == 0) {
    httpServer.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"SSID、地址和令牌不能为空\"}");
    return;
  }

  uint16_t port = (uint16_t)portStr.toInt();
  if (port == 0) port = DSH_DEFAULT_BRIDGE_PORT;

  settingsStore.setWifi(ssid, pass);
  settingsStore.setBridge(host, port, token);

  httpServer.send(200, "application/json", "{\"ok\":true}");

  // Schedule a reboot after the response is sent.
  saved_ = true;
  delay(200);
  ESP.restart();
}

void Provisioner::handleScan() {
  const int n = WiFi.scanNetworks();
  String json = "[";
  bool first = true;
  for (int i = 0; i < n; ++i) {
    if (!first) json += ",";
    // Escape the SSID for JSON (rudimentary, good enough for ASCII SSIDs).
    String ssid = WiFi.SSID(i);
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    json += "\"" + ssid + "\"";
    first = false;
  }
  json += "]";
  WiFi.scanDelete();
  httpServer.send(200, "application/json; charset=utf-8", json);
}

// ── lifecycle ──────────────────────────────────────────────────────────────

unsigned long Provisioner::uptimeMs() const {
  return active_ ? millis() - startedMs_ : 0;
}

void Provisioner::begin(const char* reason) {
  if (active_) return;
  reason_ = reason ? String(reason) : "";
  saved_ = false;

  deriveCredentials();

  Serial.printf("[prov] AP: %s / %s\n", apSsid_.c_str(), apPassword_.c_str());
  Serial.printf("[prov] reason: %s\n", reason_.c_str());

  // Start the access point.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid_.c_str(), apPassword_.c_str());
  delay(100);
  Serial.printf("[prov] AP IP: %s\n",
                WiFi.softAPIP().toString().c_str());

  // DNS: answer every query with the AP's own IP (captive portal).
  dnsServer.start(53, "*", WiFi.softAPIP());

  // HTTP
  httpServer.on("/", [this]() { handleRoot(); });
  httpServer.on("/save", HTTP_POST, [this]() { handleSave(); });
  httpServer.on("/scan", [this]() { handleScan(); });
  httpServer.onNotFound([this]() { handleNotFound(); });
  httpServer.begin();

  active_ = true;
  startedMs_ = millis();
  Serial.println("[prov] portal ready");
}

void Provisioner::loop() {
  if (!active_) return;
  dnsServer.processNextRequest();
  httpServer.handleClient();
}

void Provisioner::end() {
  if (!active_) return;
  httpServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  active_ = false;
  Serial.println("[prov] portal stopped");
}
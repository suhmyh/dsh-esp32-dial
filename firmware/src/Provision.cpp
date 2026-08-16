// Copyright (c) 2026 DSH ESP32 dial project.
// SPDX-License-Identifier: MIT
//
// Provisioning portal implementation. See Provision.h for the design rationale.

#include "Provision.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <mbedtls/md.h>

#include "Config.h"
#include "Settings.h"

Provisioner provisioner;

namespace {
DNSServer dnsServer;
WebServer httpServer(80);

static const char* kHtmlForm = R"html(
<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>连接圆盘</title>
<style>
  *{box-sizing:border-box}
  body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC","Microsoft YaHei",sans-serif;background:#0D111D;color:#e8eaf0;margin:0;padding:20px;max-width:520px;line-height:1.5}
  h1{font-size:22px;margin:0 0 6px}
  .sub{color:#8891a5;font-size:13px;margin:0 0 20px}
  .step{background:#151a28;border:1px solid #252c3d;border-radius:10px;padding:14px 16px;margin-bottom:12px}
  .step h2{font-size:14px;margin:0 0 10px;display:flex;align-items:center;gap:8px}
  .n{display:inline-flex;align-items:center;justify-content:center;width:20px;height:20px;border-radius:50%;background:#1E88E5;color:#fff;font-size:11px;font-weight:700;flex:none}
  label{display:block;margin:10px 0 4px;font-size:13px;font-weight:600}
  input[type=text],input[type=password]{width:100%;padding:11px;border:1px solid #333;border-radius:8px;background:#0a0e16;color:#fff;font-size:15px}
  input:focus{outline:2px solid #1E88E5;border-color:#1E88E5}
  .hint{font-size:12px;color:#8891a5;margin:5px 0 0}
  .hint code{background:#0a0e16;border:1px solid #252c3d;border-radius:4px;padding:1px 5px;font-size:11px}
  button{width:100%;margin-top:14px;padding:14px;border:none;border-radius:10px;background:#1E88E5;color:#fff;font-size:16px;font-weight:700;cursor:pointer}
  button:active{background:#1565C0}
  button:disabled{opacity:.5}
  .row{display:flex;gap:8px}
  .row input{flex:1}
  .btn-sm{width:auto;margin:0;padding:0 14px;background:#1a1a2e;border:1px solid #333;border-radius:8px;font-size:13px;font-weight:400;white-space:nowrap}
  #networks{display:none;margin-top:6px}
  select{width:100%;padding:10px;border:1px solid #333;border-radius:8px;background:#0a0e16;color:#fff;font-size:14px}
  select option{background:#0a0e16}
  .msg{margin-top:14px;padding:12px;border-radius:8px;display:none;font-size:14px}
  .msg.ok{background:#0f2416;color:#a8e6b8;display:block}
  .msg.err{background:#2a1010;color:#ffb4a8;display:block}
  .faq{font-size:12px;color:#8891a5}
  .faq p{margin:6px 0}
  .faq b{color:#e8eaf0}
</style>
</head>
<body>
<h1>连接你的圆盘</h1>
<p class="sub">让圆盘连上家里的 WiFi，再连到电脑上的 DSH 桥接。三分钟搞定。</p>

<div class="step">
  <h2><span class="n">1</span>让圆盘上网</h2>
  <label for="ssid">你家 WiFi 名称</label>
  <div class="row">
    <input type="text" id="ssid" placeholder="例如：ChinaNet-5G" required>
    <button type="button" class="btn-sm" onclick="scan()">查看附近</button>
  </div>
  <div id="networks">
    <select id="netlist" size="6" onchange="document.getElementById('ssid').value=this.value"></select>
  </div>
  <div class="hint">点“查看附近”自动列出，直接选即可。圆盘只支持 2.4G 网络。</div>

  <label for="pass">WiFi 密码</label>
  <input type="password" id="pass" placeholder="你家 WiFi 的密码">
  <div class="hint">开放网络可以不填。</div>
</div>

<div class="step">
  <h2><span class="n">2</span>连接电脑上的桥接</h2>
  <p class="hint">先在电脑上运行桥接程序（<code>node bridge.js</code>），它启动时会打印一行 <code>device token:</code>。</p>

  <label for="host">桥接地址</label>
  <input type="text" id="host" placeholder="例如：149.88.88.167" required>
  <div class="hint">同一局域网填电脑 IP；跨网络填公网地址。</div>

  <label for="port">端口</label>
  <input type="text" id="port" value="7002" inputmode="numeric">
  <div class="hint">局域网直连填 3083，走公网隧道填 7002。</div>

  <label for="token">设备令牌（Token）</label>
  <input type="password" id="token" placeholder="桥接窗口里的 32 位口令" required>
  <div class="hint">从桥接窗口复制。</div>
</div>

<div class="step faq">
  <h2><span class="n">3</span>保存</h2>
  <p>填完点下面按钮，圆盘会保存配置并自动重启，然后连 WiFi、连桥接，一两分钟后屏幕显示状态。</p>
  <button type="button" onclick="save()">保存并连接</button>
  <div id="result" class="msg"></div>
</div>

<script>
let scanning=false;
function $(id){return document.getElementById(id);}
async function scan(){
  if(scanning)return;
  scanning=true;
  const btn=document.querySelector('.btn-sm');
  btn.textContent='扫描中…';
  $('networks').style.display='none';
  try{
    const r=await fetch('/scan');
    const list=await r.json();
    const sel=$('netlist');
    sel.innerHTML=list.map(s=>'<option>'+s.replace(/[<>&"']/g,c=>({'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;',"'":'&#39;'}[c]))+'</option>').join('');
    if(list.length)$('networks').style.display='block';
  }catch(e){}
  btn.textContent='查看附近';
  scanning=false;
}
async function save(){
  const b=new URLSearchParams({ssid:$('ssid').value,pass:$('pass').value,host:$('host').value,port:$('port').value,token:$('token').value});
  if(!b.get('ssid')||!b.get('host')||!b.get('token')){
    const el=$('result');
    el.className='msg err';
    el.textContent='WiFi 名称、电脑地址、设备令牌三项必填';
    return;
  }
  const btn=document.querySelector('button[type=button]:last-of-type');
  btn.disabled=true;
  btn.textContent='正在保存…';
  try{
    const r=await fetch('/save',{method:'POST',body:b});
    const j=await r.json();
    const el=$('result');
    el.className='msg '+(j.ok?'ok':'err');
    el.textContent=j.ok?'已保存！圆盘正在重启并连接…':'保存失败: '+j.error;
  }catch(e){
    const el=$('result');
    el.className='msg err';
    el.textContent='网络错误: '+e.message;
    btn.disabled=false;btn.textContent='保存并连接';
  }
}
// Prefill the form with what the dial is actually configured to use, so
// changing one field does not mean retyping the rest. The password and token
// are deliberately NOT sent back — a value already saved should not be
// readable from a page anyone on the AP can open — so those two stay blank and
// are only written when non-empty (see /save).
async function loadCurrent(){
  try{
    const r=await fetch('/current');
    const j=await r.json();
    if(j.ssid)$('ssid').value=j.ssid;
    if(j.host)$('host').value=j.host;
    if(j.port)$('port').value=j.port;
    if(j.hasPass)$('pass').placeholder='已保存，留空则不修改';
    if(j.hasToken)$('token').placeholder='已保存，留空则不修改';
  }catch(e){}
}
loadCurrent();
</script>
</body>
</html>
)html";
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

/**
 * Report what the dial is currently configured to use, for form prefill.
 *
 * Password and token are never returned: the page is served on an open AP the
 * visitor joined from their phone, and a bystander could read saved secrets.
 * The booleans just tell the form that those fields already hold values, so
 * leaving them blank on save means "keep what we have".
 */
void Provisioner::handleCurrent() {
  const DialSettings& cfg = settingsStore.get();
  snprintf(jsonBuf_, sizeof(jsonBuf_),
           "{\"ssid\":\"%s\",\"host\":\"%s\",\"port\":%u,"
           "\"hasPass\":%s,\"hasToken\":%s}",
           cfg.wifiSsid.c_str(), cfg.bridgeHost.c_str(), cfg.bridgePort,
           cfg.wifiPassword.length() > 0 ? "true" : "false",
           cfg.bridgeToken.length() > 0 ? "true" : "false");
  httpServer.send(200, "application/json; charset=utf-8", jsonBuf_);
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

  // Password and token may be left blank to mean "keep the saved one", which is
  // what the prefilled form promises. Only SSID and host are truly required,
  // because those are shown in the form and can always be re-entered.
  const DialSettings& current = settingsStore.get();
  const String effectivePass = pass.length() > 0 ? pass : current.wifiPassword;
  const String effectiveToken = token.length() > 0 ? token : current.bridgeToken;

  if (ssid.length() == 0 || host.length() == 0 || effectiveToken.length() == 0) {
    httpServer.send(400, "application/json",
                    "{\"ok\":false,\"error\":\"WiFi 名称、地址和令牌不能为空\"}");
    return;
  }

  uint16_t port = (uint16_t)portStr.toInt();
  if (port == 0) port = DSH_DEFAULT_BRIDGE_PORT;

  settingsStore.setWifi(ssid, effectivePass);
  settingsStore.setBridge(host, port, effectiveToken);

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
  httpServer.on("/current", [this]() { handleCurrent(); });
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
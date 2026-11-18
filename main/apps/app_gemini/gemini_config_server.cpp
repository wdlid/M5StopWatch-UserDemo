/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "gemini_config_server.h"

#include <ArduinoJson.h>
#include <dns_server.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <hal/hal.h>
#include <lwip/ip_addr.h>
#include <mooncake_log.h>

#include <cstdio>
#include <cstring>

namespace gemini {

namespace {

constexpr const char* _tag = "Gemini-WebCfg";

const char GEMINI_CONFIG_HTML[] = R"rawliteral(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>M5StopWatch Gemini 3.8 Live 配置</title>
<style>
  :root {
    --bg: #0b0f19;
    --card: #151c2c;
    --border: #263147;
    --accent: #4ad78c;
    --accent-dark: #0f5831;
    --text: #f1f5f9;
    --muted: #94a3b8;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 16px;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    background: var(--bg); color: var(--text);
  }
  .container { max-width: 540px; margin: 0 auto; }
  .header { text-align: center; margin-bottom: 18px; }
  .header h1 { font-size: 20px; margin: 0 0 6px; color: var(--accent); }
  .header p { font-size: 13px; margin: 0; color: var(--muted); }
  .card {
    background: var(--card); border: 1px solid var(--border);
    border-radius: 14px; padding: 16px; margin-bottom: 14px;
  }
  .card h2 {
    font-size: 15px; margin: 0 0 12px; color: #38bdf8;
    border-bottom: 1px solid var(--border); padding-bottom: 8px;
  }
  label { display: block; font-size: 13px; color: var(--muted); margin-bottom: 5px; margin-top: 10px; }
  input[type="text"], input[type="password"], select, textarea {
    width: 100%; padding: 10px 12px; border-radius: 8px;
    border: 1px solid var(--border); background: #0f1523;
    color: var(--text); font-size: 14px; outline: none;
  }
  textarea { min-height: 80px; resize: vertical; }
  .row { display: flex; align-items: center; justify-content: space-between; margin-top: 10px; }
  .row label { margin: 0; color: var(--text); font-size: 14px; }
  input[type="checkbox"] { width: 20px; height: 20px; accent-color: var(--accent); }
  .btn {
    width: 100%; padding: 14px; border: none; border-radius: 999px;
    background: var(--accent); color: var(--accent-dark);
    font-size: 16px; font-weight: 700; cursor: pointer; margin-top: 8px;
  }
  .toast {
    text-align: center; font-size: 14px; margin-top: 12px;
    padding: 10px; border-radius: 8px; display: none;
  }
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <h1>Gemini 3.8 Live 实时语音配置</h1>
    <p>支持 Cloudflare Worker 二进制网关 + DoH &amp; ECH 防阻断</p>
  </div>

  <div class="card">
    <h2>1. 网关与模型设置</h2>
    <label>Cloudflare Worker / WebSocket 地址</label>
    <input type="text" id="ws_url" placeholder="wss://xiaozhi-gemini-live-gateway.testad.workers.dev/ws">

    <div class="row">
      <label>启用 Worker 原生二进制 PCM 流模式 (省流降载)</label>
      <input type="checkbox" id="bin_mode" checked>
    </div>

    <label>Worker 鉴权 Token (WS_AUTH_TOKEN，未设置可留空)</label>
    <input type="password" id="ws_token" placeholder="可选 Bearer Token">

    <label>Gemini API Key (若 Worker 已配置服务端密钥可留空)</label>
    <input type="password" id="api_key" placeholder="AIzaSy...">

    <label>Gemini Live 模型名称</label>
    <input type="text" id="model" placeholder="models/gemini-3.8-live">

    <label>语音合成音色 (Voice)</label>
    <select id="voice">
      <option value="Leda">Leda (亲切自然女声 - 推荐)</option>
      <option value="Aoede">Aoede (温柔女声)</option>
      <option value="Puck">Puck (活泼男声)</option>
      <option value="Kore">Kore (知性女声)</option>
      <option value="Charon">Charon (沉稳男声)</option>
      <option value="Fenrir">Fenrir (磁性男声)</option>
    </select>

    <label>系统提示词 (System Prompt)</label>
    <textarea id="prompt"></textarea>
  </div>

  <div class="card">
    <h2>2. Cloudflare ECH &amp; DoH 优选穿透</h2>
    <div class="row">
      <label>启用 TLS 1.3 ECH / 外层 SNI 隐藏</label>
      <input type="checkbox" id="ech_en" checked>
    </div>

    <div class="row">
      <label>启用 RFC 8484 DoH 安全解析</label>
      <input type="checkbox" id="doh_en" checked>
    </div>

    <label>优选 Cloudflare Anycast IP (选填，如 172.64.229.205)</label>
    <input type="text" id="conn_ip" placeholder="留空则通过 DoH 自动解析最优节点">

    <label>ECH 外层公网域名 (Public Name)</label>
    <input type="text" id="ech_dom" placeholder="cloudflare-ech.com">

    <label>主 DoH 服务器地址</label>
    <input type="text" id="doh_url1" placeholder="https://edge.1molchuan.top/linuxdo">

    <label>备用 DoH 服务器地址</label>
    <input type="text" id="doh_url2" placeholder="https://edge.1molchuan.top/plus">

    <label>静态 ECHConfigList (Base64，选填)</label>
    <input type="text" id="ech_b64" placeholder="留空则通过 DoH HTTPS(Type 65) 动态拉取">
  </div>

  <button class="btn" onclick="saveConfig()">保存并应用配置</button>
  <div id="toast" class="toast"></div>
</div>

<script>
async function loadConfig() {
  try {
    const r = await fetch('/gemini/config');
    const d = await r.json();
    document.getElementById('ws_url').value = d.wsUrl || '';
    document.getElementById('bin_mode').checked = d.binaryAudioMode !== false;
    document.getElementById('ws_token').value = d.wsToken || '';
    document.getElementById('api_key').value = d.apiKey || '';
    document.getElementById('model').value = d.model || 'models/gemini-3.8-live';
    document.getElementById('voice').value = d.voice || 'Leda';
    document.getElementById('prompt').value = d.systemPrompt || '';
    document.getElementById('ech_en').checked = d.echEnabled !== false;
    document.getElementById('doh_en').checked = d.dohEnabled !== false;
    document.getElementById('conn_ip').value = d.connectIp || '';
    document.getElementById('ech_dom').value = d.echConfigDomain || 'cloudflare-ech.com';
    document.getElementById('doh_url1').value = d.dohUrl1 || '';
    document.getElementById('doh_url2').value = d.dohUrl2 || '';
    document.getElementById('ech_b64').value = d.staticEchConfigB64 || '';
  } catch (e) {
    console.error(e);
  }
}

async function saveConfig() {
  const payload = {
    wsUrl: document.getElementById('ws_url').value.trim(),
    binaryAudioMode: document.getElementById('bin_mode').checked,
    wsToken: document.getElementById('ws_token').value.trim(),
    apiKey: document.getElementById('api_key').value.trim(),
    model: document.getElementById('model').value.trim(),
    voice: document.getElementById('voice').value,
    systemPrompt: document.getElementById('prompt').value.trim(),
    echEnabled: document.getElementById('ech_en').checked,
    dohEnabled: document.getElementById('doh_en').checked,
    connectIp: document.getElementById('conn_ip').value.trim(),
    echConfigDomain: document.getElementById('ech_dom').value.trim(),
    dohUrl1: document.getElementById('doh_url1').value.trim(),
    dohUrl2: document.getElementById('doh_url2').value.trim(),
    staticEchConfigB64: document.getElementById('ech_b64').value.trim()
  };
  const t = document.getElementById('toast');
  try {
    const r = await fetch('/gemini/config', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(payload)
    });
    if (r.ok) {
      t.style.display = 'block';
      t.style.background = '#14532d';
      t.style.color = '#86efac';
      t.textContent = '配置已保存到手表 NVS！手表即将自动重连 Gemini Live。';
    } else {
      throw new Error('Save failed');
    }
  } catch (e) {
    t.style.display = 'block';
    t.style.background = '#7f1d1d';
    t.style.color = '#fca5a5';
    t.textContent = '保存失败: ' + e.message;
  }
}

loadConfig();
</script>
</body>
</html>
)rawliteral";

std::string make_ap_ssid()
{
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
        return "M5StopWatch-AI";
    }
    char ssid[32] = {};
    std::snprintf(ssid, sizeof(ssid), "M5StopWatch-AI-%02X%02X", mac[4], mac[5]);
    return std::string(ssid);
}

}  // namespace

GeminiConfigServer::GeminiConfigServer() = default;

GeminiConfigServer::~GeminiConfigServer()
{
    stop();
}

bool GeminiConfigServer::start()
{
    if (_running.load()) {
        return true;
    }

    _config_saved = false;
    auto wifi     = GetHAL().getWifiStatus();
    if (wifi.connected && !wifi.ipAddress.empty()) {
        _ssid       = wifi.ssid;
        _page_url   = "http://" + wifi.ipAddress + "/gemini";
        _started_ap = false;
    } else {
        GetHAL().wifi_enter_badge_ap();
        esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (!ap_netif) {
            ap_netif = esp_netif_create_default_wifi_ap();
        }
        if (!ap_netif) {
            GetHAL().wifi_exit_badge_ap();
            return false;
        }
        _ap_netif = ap_netif;

        esp_netif_ip_info_t ip_info;
        IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
        IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_set_ip_info(ap_netif, &ip_info);
        esp_netif_dhcps_start(ap_netif);

        auto* dns = new DnsServer();
        dns->Start(ip_info.gw);
        _dns_server = dns;

        _ssid     = make_ap_ssid();
        _page_url = "http://192.168.4.1/gemini";

        wifi_config_t wifi_config = {};
        std::strncpy(reinterpret_cast<char*>(wifi_config.ap.ssid), _ssid.c_str(), sizeof(wifi_config.ap.ssid) - 1);
        wifi_config.ap.ssid_len       = _ssid.size();
        wifi_config.ap.max_connection = 4;
        wifi_config.ap.authmode       = WIFI_AUTH_OPEN;

        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_AP);
        esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
        esp_wifi_set_ps(WIFI_PS_NONE);
        esp_wifi_start();
        _started_ap = true;
    }

    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size       = 8192;
    config.uri_match_fn     = httpd_uri_match_wildcard;

    if (httpd_start(&_server, &config) != ESP_OK) {
        stop();
        return false;
    }

    httpd_uri_t page_uri = {
        .uri      = "/gemini",
        .method   = HTTP_GET,
        .handler  = &GeminiConfigServer::handlePage,
        .user_ctx = this,
    };
    httpd_register_uri_handler(_server, &page_uri);

    httpd_uri_t get_cfg_uri = {
        .uri      = "/gemini/config",
        .method   = HTTP_GET,
        .handler  = &GeminiConfigServer::handleGetConfig,
        .user_ctx = this,
    };
    httpd_register_uri_handler(_server, &get_cfg_uri);

    httpd_uri_t post_cfg_uri = {
        .uri      = "/gemini/config",
        .method   = HTTP_POST,
        .handler  = &GeminiConfigServer::handleSaveConfig,
        .user_ctx = this,
    };
    httpd_register_uri_handler(_server, &post_cfg_uri);

    httpd_uri_t root_uri = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = &GeminiConfigServer::handlePage,
        .user_ctx = this,
    };
    httpd_register_uri_handler(_server, &root_uri);

    _running = true;
    mclog::tagInfo(_tag, "Started Gemini config web server at {}", _page_url);
    return true;
}

void GeminiConfigServer::stop()
{
    if (_server != nullptr) {
        httpd_stop(_server);
        _server = nullptr;
    }

    if (_started_ap) {
        if (_dns_server != nullptr) {
            static_cast<DnsServer*>(_dns_server)->Stop();
            delete static_cast<DnsServer*>(_dns_server);
            _dns_server = nullptr;
        }
        esp_wifi_stop();
        if (_ap_netif != nullptr) {
            esp_netif_destroy_default_wifi(_ap_netif);
            _ap_netif = nullptr;
        }
        GetHAL().wifi_exit_badge_ap();
        _started_ap = false;
    }

    _running = false;
}

esp_err_t GeminiConfigServer::handlePage(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, GEMINI_CONFIG_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t GeminiConfigServer::handleGetConfig(httpd_req_t* req)
{
    GeminiConfig cfg = GeminiConfig::loadFromNvs();
    JsonDocument doc;
    doc["wsUrl"]              = cfg.wsUrl;
    doc["apiKey"]             = cfg.apiKey;
    doc["wsToken"]            = cfg.wsToken;
    doc["model"]              = cfg.model;
    doc["voice"]              = cfg.voice;
    doc["thinking"]           = cfg.thinking;
    doc["binaryAudioMode"]    = cfg.binaryAudioMode;
    doc["systemPrompt"]       = cfg.systemPrompt;
    doc["dohEnabled"]         = cfg.dohEch.dohEnabled;
    doc["dohUrl1"]            = cfg.dohEch.dohUrl1;
    doc["dohUrl2"]            = cfg.dohEch.dohUrl2;
    doc["echEnabled"]         = cfg.dohEch.echEnabled;
    doc["echConfigDomain"]    = cfg.dohEch.echConfigDomain;
    doc["staticEchConfigB64"] = cfg.dohEch.staticEchConfigB64;
    doc["connectIp"]          = cfg.dohEch.connectIp;

    std::string out;
    serializeJson(doc, out);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, out.c_str(), out.size());
}

esp_err_t GeminiConfigServer::handleSaveConfig(httpd_req_t* req)
{
    if (req->content_len <= 0 || req->content_len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
        return ESP_FAIL;
    }

    std::string body(req->content_len, '\0');
    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (r <= 0) {
            return ESP_FAIL;
        }
        received += r;
    }

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    GeminiConfig cfg = GeminiConfig::loadFromNvs();
    if (doc["wsUrl"].is<const char*>()) cfg.wsUrl = doc["wsUrl"].as<std::string>();
    if (doc["apiKey"].is<const char*>()) cfg.apiKey = doc["apiKey"].as<std::string>();
    if (doc["wsToken"].is<const char*>()) cfg.wsToken = doc["wsToken"].as<std::string>();
    if (doc["model"].is<const char*>()) cfg.model = doc["model"].as<std::string>();
    if (doc["voice"].is<const char*>()) cfg.voice = doc["voice"].as<std::string>();
    if (doc["systemPrompt"].is<const char*>()) cfg.systemPrompt = doc["systemPrompt"].as<std::string>();
    if (doc["binaryAudioMode"].is<bool>()) cfg.binaryAudioMode = doc["binaryAudioMode"].as<bool>();

    if (doc["dohEnabled"].is<bool>()) cfg.dohEch.dohEnabled = doc["dohEnabled"].as<bool>();
    if (doc["dohUrl1"].is<const char*>()) cfg.dohEch.dohUrl1 = doc["dohUrl1"].as<std::string>();
    if (doc["dohUrl2"].is<const char*>()) cfg.dohEch.dohUrl2 = doc["dohUrl2"].as<std::string>();
    if (doc["echEnabled"].is<bool>()) cfg.dohEch.echEnabled = doc["echEnabled"].as<bool>();
    if (doc["echConfigDomain"].is<const char*>()) cfg.dohEch.echConfigDomain = doc["echConfigDomain"].as<std::string>();
    if (doc["staticEchConfigB64"].is<const char*>())
        cfg.dohEch.staticEchConfigB64 = doc["staticEchConfigB64"].as<std::string>();
    if (doc["connectIp"].is<const char*>()) cfg.dohEch.connectIp = doc["connectIp"].as<std::string>();

    cfg.saveToNvs();

    auto* self = static_cast<GeminiConfigServer*>(req->user_ctx);
    if (self != nullptr) {
        self->_config_saved = true;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, R"({"status":"ok"})");
}

}  // namespace gemini

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
<title>M5StopWatch · Gemini Live 配置</title>
<!-- Google Fonts CDN (Zero font files in firmware) -->
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Newsreader:ital,opsz,wght@0,6..72,400..600;1,6..72,400&family=Plus+Jakarta+Sans:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
<style>
  :root {
    --bg: #141311;
    --card: #1c1a17;
    --card-sub: #171513;
    --border: #322e28;
    --border-hover: #48433b;
    --accent: #d97757;
    --accent-hover: #e28364;
    --accent-dim: rgba(217, 119, 87, 0.12);
    --accent-border: rgba(217, 119, 87, 0.28);
    --text: #f4f2ec;
    --text-sub: #b5b1a6;
    --text-muted: #7a766c;
    --success: #4ade80;
    --success-bg: rgba(74, 222, 128, 0.12);
    --danger: #f87171;
    --danger-bg: rgba(248, 113, 113, 0.12);
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: var(--bg);
    background-image: radial-gradient(circle at 50% 0%, #25221c 0%, #141311 75%);
    background-attachment: fixed;
    color: var(--text);
    font-family: 'Plus Jakarta Sans', -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    line-height: 1.5;
    padding: 24px 16px 40px;
    -webkit-font-smoothing: antialiased;
  }
  .container { max-width: 560px; margin: 0 auto; }
  
  /* Header */
  .header { text-align: center; margin-bottom: 24px; padding-top: 8px; }
  .brand-badge {
    display: inline-flex; align-items: center; gap: 6px;
    background: var(--accent-dim); color: var(--accent);
    border: 1px solid var(--accent-border);
    padding: 4px 12px; border-radius: 999px;
    font-size: 11px; font-weight: 600; letter-spacing: 0.5px;
    text-transform: uppercase; margin-bottom: 12px;
  }
  .brand-badge svg { width: 12px; height: 12px; fill: currentColor; }
  .header h1 {
    font-family: 'Newsreader', Georgia, serif;
    font-size: 28px; font-weight: 500; letter-spacing: -0.3px;
    color: var(--text); margin-bottom: 6px;
  }
  .header p {
    font-size: 13px; color: var(--text-sub);
    max-width: 420px; margin: 0 auto;
  }

  /* Cards */
  .card {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: 16px;
    padding: 20px;
    margin-bottom: 16px;
    box-shadow: 0 4px 20px rgba(0, 0, 0, 0.25);
    transition: border-color 0.2s ease;
  }
  .card:hover { border-color: var(--border-hover); }
  .card-header {
    display: flex; align-items: center; justify-content: space-between;
    padding-bottom: 14px; margin-bottom: 14px;
    border-bottom: 1px solid var(--border);
  }
  .card-title {
    font-family: 'Newsreader', Georgia, serif;
    font-size: 18px; font-weight: 500; color: var(--text);
  }
  .card-tag {
    font-size: 11px; font-weight: 500; color: var(--text-muted);
    background: var(--card-sub); padding: 3px 8px; border-radius: 6px;
    border: 1px solid var(--border);
  }

  /* Form Elements */
  .form-group { margin-bottom: 14px; }
  .form-group:last-child { margin-bottom: 0; }
  label {
    display: block; font-size: 12px; font-weight: 600;
    color: var(--text-sub); text-transform: uppercase;
    letter-spacing: 0.5px; margin-bottom: 6px;
  }
  .label-hint {
    display: block; font-size: 11px; color: var(--text-muted);
    font-weight: 400; text-transform: none; letter-spacing: normal;
    margin-top: 4px;
  }
  input[type="text"], input[type="password"], select, textarea {
    width: 100%; padding: 11px 14px; border-radius: 10px;
    border: 1px solid var(--border); background: var(--card-sub);
    color: var(--text); font-size: 13.5px; outline: none;
    transition: border-color 0.2s, box-shadow 0.2s;
  }
  input.mono, textarea.mono {
    font-family: 'JetBrains Mono', Consolas, monospace;
    font-size: 12.5px;
  }
  input[type="text"]:focus, input[type="password"]:focus, select:focus, textarea:focus {
    border-color: var(--accent);
    box-shadow: 0 0 0 3px var(--accent-dim);
  }
  textarea { min-height: 88px; resize: vertical; line-height: 1.4; }
  select { cursor: pointer; }

  /* Toggles / Switches */
  .toggle-row {
    display: flex; align-items: center; justify-content: space-between;
    padding: 10px 0; border-bottom: 1px solid rgba(255, 255, 255, 0.04);
  }
  .toggle-row:last-child { border-bottom: none; }
  .toggle-info { flex: 1; padding-right: 16px; }
  .toggle-title { font-size: 13.5px; font-weight: 500; color: var(--text); }
  .toggle-desc { font-size: 11.5px; color: var(--text-muted); margin-top: 2px; }

  .switch {
    position: relative; display: inline-block;
    width: 44px; height: 26px; flex-shrink: 0;
  }
  .switch input { opacity: 0; width: 0; height: 0; }
  .slider {
    position: absolute; cursor: pointer; inset: 0;
    background-color: #2b2722; border: 1px solid var(--border);
    transition: 0.25s cubic-bezier(0.4, 0, 0.2, 1);
    border-radius: 999px;
  }
  .slider:before {
    position: absolute; content: ""; height: 18px; width: 18px;
    left: 3px; bottom: 3px; background-color: #8c877d;
    transition: 0.25s cubic-bezier(0.4, 0, 0.2, 1);
    border-radius: 50%;
  }
  input:checked + .slider {
    background-color: var(--accent);
    border-color: var(--accent);
  }
  input:checked + .slider:before {
    transform: translateX(18px);
    background-color: #ffffff;
  }

  /* Action Button */
  .btn-submit {
    width: 100%; padding: 14px; border: none; border-radius: 999px;
    background: var(--accent); color: #ffffff;
    font-size: 15px; font-weight: 600; cursor: pointer;
    letter-spacing: 0.2px; margin-top: 10px;
    transition: background-color 0.2s, transform 0.1s;
    box-shadow: 0 4px 16px rgba(217, 119, 87, 0.3);
  }
  .btn-submit:hover { background: var(--accent-hover); }
  .btn-submit:active { transform: scale(0.99); }

  /* Toast Notification */
  .toast {
    margin-top: 14px; padding: 12px 16px; border-radius: 12px;
    font-size: 13px; text-align: center; display: none;
    animation: fadeIn 0.3s ease;
  }
  @keyframes fadeIn {
    from { opacity: 0; transform: translateY(-4px); }
    to { opacity: 1; transform: translateY(0); }
  }
  .toast.success {
    background: var(--success-bg); color: var(--success);
    border: 1px solid rgba(74, 222, 128, 0.25);
  }
  .toast.error {
    background: var(--danger-bg); color: var(--danger);
    border: 1px solid rgba(248, 113, 113, 0.25);
  }
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <div class="brand-badge">
      <svg viewBox="0 0 24 24"><path d="M12 2L15.09 8.26L22 9.27L17 14.14L18.18 21.02L12 17.77L5.82 21.02L7 14.14L2 9.27L8.91 8.26L12 2Z"/></svg>
      M5StopWatch · Live Audio
    </div>
    <h1>Gemini 3.8 实时语音网关</h1>
    <p>Anthropic 质感配色与排版 · 支持 Cloudflare Worker 二进制穿透与 ECH/DoH 防阻断</p>
  </div>

  <div class="card">
    <div class="card-header">
      <div class="card-title">1. 服务网关与语音模型</div>
      <div class="card-tag">Gateway &amp; Model</div>
    </div>

    <div class="form-group">
      <label>WebSocket 网关地址 (Worker / Gemini Direct)</label>
      <input type="text" id="ws_url" class="mono" placeholder="wss://xiaozhi-gemini-live-gateway.testad.workers.dev/ws">
      <span class="label-hint">支持 Cloudflare Worker 中转网关或官方 WebSocket 地址</span>
    </div>

    <div class="toggle-row">
      <div class="toggle-info">
        <div class="toggle-title">原生二进制音频流模式 (Binary PCM)</div>
        <div class="toggle-desc">使用 Worker 原生二进制传输，节省 33% 流量并大幅降低 ESP32 运算载荷</div>
      </div>
      <label class="switch">
        <input type="checkbox" id="bin_mode" checked>
        <span class="slider"></span>
      </label>
    </div>

    <div class="form-group" style="margin-top:12px;">
      <label>Worker 鉴权令牌 (WS_AUTH_TOKEN)</label>
      <input type="password" id="ws_token" class="mono" placeholder="可选 Bearer Token">
      <span class="label-hint">若 Worker 开启了鉴权保护请输入；直连 Google 时留空</span>
    </div>

    <div class="form-group">
      <label>Gemini API 密钥 (API Key)</label>
      <input type="password" id="api_key" class="mono" placeholder="AIzaSy...">
      <span class="label-hint">若 Worker 服务端已注入密钥可留空；直连 Google 时必填</span>
    </div>

    <div class="form-group">
      <label>模型名称 (Model Identifier)</label>
      <input type="text" id="model" class="mono" placeholder="models/gemini-3.8-live">
    </div>

    <div class="form-group">
      <label>思考等级 (Thinking Level)</label>
      <select id="thinking">
        <option value="LOW">LOW (快速响应，推荐日常对话)</option>
        <option value="HIGH">HIGH (深度思考，逻辑与复杂问答更佳)</option>
      </select>
    </div>

    <div class="form-group">
      <label>合成音色 (Voice Timbre)</label>
      <select id="voice">
        <option value="Leda">Leda (亲切自然女声 - 推荐)</option>
        <option value="Aoede">Aoede (温柔知性女声)</option>
        <option value="Puck">Puck (活泼清朗男声)</option>
        <option value="Kore">Kore (端庄成熟女声)</option>
        <option value="Charon">Charon (深沉稳重男声)</option>
        <option value="Fenrir">Fenrir (磁性质感男声)</option>
      </select>
    </div>

    <div class="form-group">
      <label>系统人设提示词 (System Prompt)</label>
      <textarea id="prompt" placeholder="你是一个随身智能语音手表助手..."></textarea>
    </div>
  </div>

  <div class="card">
    <div class="card-header">
      <div class="card-title">2. 网络穿透与防阻断加速</div>
      <div class="card-tag">ECH &amp; DoH Tunnel</div>
    </div>

    <div class="toggle-row">
      <div class="toggle-info">
        <div class="toggle-title">启用 TLS 1.3 ECH / 外层 SNI 混淆</div>
        <div class="toggle-desc">使用 cloudflare-ech.com 隐藏真实的 *.workers.dev 域名，避免明文 SNI 阻断</div>
      </div>
      <label class="switch">
        <input type="checkbox" id="ech_en" checked>
        <span class="slider"></span>
      </label>
    </div>

    <div class="toggle-row">
      <div class="toggle-info">
        <div class="toggle-title">启用 RFC 8484 DNS-over-HTTPS (DoH)</div>
        <div class="toggle-desc">安全加密解析 IP 与 ECHConfigList，防范 DNS 污染与劫持</div>
      </div>
      <label class="switch">
        <input type="checkbox" id="doh_en" checked>
        <span class="slider"></span>
      </label>
    </div>

    <div class="form-group" style="margin-top:12px;">
      <label>静态优选 Anycast IP (选填)</label>
      <input type="text" id="conn_ip" class="mono" placeholder="例如 172.64.229.205 (留空则通过 DoH 自动优选)">
    </div>

    <div class="form-group">
      <label>ECH 外层公网域名 (Public Name)</label>
      <input type="text" id="ech_dom" class="mono" placeholder="cloudflare-ech.com">
    </div>

    <div class="form-group">
      <label>主 DoH 解析节点</label>
      <input type="text" id="doh_url1" class="mono" placeholder="https://edge.1molchuan.top/linuxdo">
    </div>

    <div class="form-group">
      <label>备用 DoH 解析节点</label>
      <input type="text" id="doh_url2" class="mono" placeholder="https://edge.1molchuan.top/plus">
    </div>

    <div class="form-group">
      <label>静态 ECHConfigList (Base64，选填)</label>
      <input type="text" id="ech_b64" class="mono" placeholder="留空则通过 DoH HTTPS(Type 65) 动态拉取">
    </div>
  </div>

  <button class="btn-submit" onclick="saveConfig()">保存并立即应用配置</button>
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
    document.getElementById('thinking').value = d.thinking || 'LOW';
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
    thinking: document.getElementById('thinking').value,
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
      t.className = 'toast success';
      t.style.display = 'block';
      t.textContent = '✓ 配置已安全保存至手表 NVS！手表即将自动重新连接 Gemini Live。';
    } else {
      throw new Error('Save failed');
    }
  } catch (e) {
    t.className = 'toast error';
    t.style.display = 'block';
    t.textContent = '✕ 保存失败: ' + e.message;
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
    if (doc["thinking"].is<const char*>()) cfg.thinking = doc["thinking"].as<std::string>();
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

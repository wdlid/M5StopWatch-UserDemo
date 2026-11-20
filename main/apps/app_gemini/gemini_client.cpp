/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "gemini_client.h"

#include <ArduinoJson.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <apps/common/audio/audio.h>
#include <mooncake_log.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_wifi.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace gemini {

namespace {

constexpr const char* _tag         = "GeminiLive";
constexpr const char* _settings_ns = "gemini";

struct CityCodeEntry {
    const char* name;
    const char* code;
};

constexpr CityCodeEntry kCityCodes[] = {
    {"北京丰台", "101010900"}, {"丰台", "101010900"},   {"北京市丰台区", "101010900"},
    {"海淀", "101010200"},     {"朝阳", "101010300"},   {"东城", "101011600"},
    {"西城", "101011700"},     {"石景山", "101011000"}, {"通州", "101010600"},
    {"昌平", "101010700"},     {"大兴", "101011100"},   {"顺义", "101010400"},
    {"北京", "101010100"},     {"上海", "101020100"},   {"广州", "101280101"},
    {"深圳", "101280601"},     {"杭州", "101210101"},   {"成都", "101270101"},
    {"南京", "101190101"},     {"武汉", "101200101"},   {"西安", "101110101"},
    {"重庆", "101040100"},     {"天津", "101030100"},   {"苏州", "101190401"},
    {"长沙", "101250101"},     {"郑州", "101180101"},   {"青岛", "101120201"},
    {"沈阳", "101070101"},     {"大连", "101070201"},   {"厦门", "101230201"},
    {"福州", "101230101"},     {"济南", "101120101"},   {"合肥", "101220101"},
    {"昆明", "101290101"},     {"哈尔滨", "101050101"}, {"长春", "101060101"},
    {"石家庄", "101090101"},   {"太原", "101100101"},   {"南昌", "101240101"},
    {"南宁", "101300101"},     {"贵阳", "101260101"},   {"兰州", "101160101"},
    {"海口", "101310101"},     {"三亚", "101310201"},   {"珠海", "101280701"},
    {"佛山", "101280800"},     {"东莞", "101281601"},   {"宁波", "101210401"},
    {"无锡", "101190201"},
};

const char* resolve_city_code(const std::string& city)
{
    if (city.empty()) {
        return "101010900";  // Default: Beijing Fengtai
    }
    for (const auto& entry : kCityCodes) {
        if (city.find(entry.name) != std::string::npos) {
            return entry.code;
        }
    }
    return "101010900";
}

struct WeatherRespBuf {
    std::string body;
};

esp_err_t weather_http_cb(esp_http_client_event_t* evt)
{
    auto* b = static_cast<WeatherRespBuf*>(evt->user_data);
    if (evt->event_id == HTTP_EVENT_ON_DATA && b != nullptr && evt->data != nullptr && evt->data_len > 0) {
        if (b->body.size() + evt->data_len <= 8192) {
            b->body.append(static_cast<const char*>(evt->data), evt->data_len);
        }
    }
    return ESP_OK;
}

std::string query_weather_itboy(const std::string& city)
{
    const char* code = resolve_city_code(city);
    std::string url  = std::string("http://t.weather.itboy.net/api/weather/city/") + code;

    WeatherRespBuf resp_buf;
    esp_http_client_config_t cfg = {};
    cfg.url                      = url.c_str();
    cfg.method                   = HTTP_METHOD_GET;
    cfg.timeout_ms               = 4000;
    cfg.event_handler            = weather_http_cb;
    cfg.user_data                = &resp_buf;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        return R"({"status":"error","message":"HTTP client init failed"})";
    }

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || resp_buf.body.empty()) {
        return R"({"status":"error","message":"Weather API request failed"})";
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp_buf.body) != DeserializationError::Ok) {
        return R"({"status":"error","message":"Weather JSON parse error"})";
    }

    JsonDocument out;
    out["status"]  = "ok";
    out["city"]    = doc["cityInfo"]["city"] | (city.empty() ? "北京市丰台区" : city.c_str());
    out["wendu"]   = doc["data"]["wendu"] | "";
    out["shidu"]   = doc["data"]["shidu"] | "";
    out["quality"] = doc["data"]["quality"] | "";
    out["pm25"]    = doc["data"]["pm25"] | 0;
    out["ganmao"]  = doc["data"]["ganmao"] | "";

    if (doc["data"]["forecast"].is<JsonArray>() && doc["data"]["forecast"].size() > 0) {
        JsonObject today = doc["data"]["forecast"][0];
        out["type"]      = today["type"] | "";
        out["high"]      = today["high"] | "";
        out["low"]       = today["low"] | "";
        out["fx"]        = today["fx"] | "";
        out["fl"]        = today["fl"] | "";
        out["notice"]    = today["notice"] | "";
    }

    std::string res;
    serializeJson(out, res);
    return res;
}

std::string get_current_datetime_str()
{
    auto d = GetHAL().getDateYmd();
    auto t = GetHAL().getTimeHms();
    char buf[64] = {};
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", d.year, d.month, d.day, t.hour, t.minute,
                  t.second);
    return std::string(buf);
}

std::string encode_base64_str(const uint8_t* data, std::size_t len)
{
    std::size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &olen, data, len);
    std::vector<unsigned char> buf(olen + 4, 0);
    if (mbedtls_base64_encode(buf.data(), buf.size(), &olen, data, len) != 0) {
        return {};
    }
    return std::string(reinterpret_cast<char*>(buf.data()), olen);
}

bool decode_base64_bytes(const char* b64, std::size_t b64_len, std::vector<uint8_t>& out)
{
    if (b64 == nullptr || b64_len == 0) return false;
    std::size_t olen = 0;
    mbedtls_base64_decode(nullptr, 0, &olen, reinterpret_cast<const unsigned char*>(b64), b64_len);
    if (olen == 0) return false;
    out.resize(olen + 4);
    if (mbedtls_base64_decode(out.data(), out.size(), &olen, reinterpret_cast<const unsigned char*>(b64), b64_len) !=
        0) {
        out.clear();
        return false;
    }
    out.resize(olen);
    return !out.empty();
}

}  // namespace

GeminiConfig GeminiConfig::loadFromNvs()
{
    GeminiConfig cfg;
    Settings s(_settings_ns, false);
    cfg.wsUrl           = s.GetString("ws_url", cfg.wsUrl);
    cfg.apiKey          = s.GetString("api_key", cfg.apiKey);
    cfg.wsToken         = s.GetString("ws_token", cfg.wsToken);
    cfg.model           = s.GetString("model", cfg.model);
    cfg.voice           = s.GetString("voice", cfg.voice);
    cfg.thinking        = s.GetString("thinking", cfg.thinking);
    cfg.binaryAudioMode = s.GetBool("bin_mode", cfg.binaryAudioMode);
    cfg.systemPrompt    = s.GetString("prompt", cfg.systemPrompt);

    cfg.dohEch.dohEnabled = s.GetBool("doh_en", cfg.dohEch.dohEnabled);
    cfg.dohEch.dohUrl1    = s.GetString("doh_url1", cfg.dohEch.dohUrl1);
    cfg.dohEch.dohUrl2    = s.GetString("doh_url2", cfg.dohEch.dohUrl2);
    cfg.dohEch.connectIp  = s.GetString("conn_ip", cfg.dohEch.connectIp);
    return cfg;
}

void GeminiConfig::saveToNvs() const
{
    Settings s(_settings_ns, true);
    s.SetString("ws_url", wsUrl);
    s.SetString("api_key", apiKey);
    s.SetString("ws_token", wsToken);
    s.SetString("model", model);
    s.SetString("voice", voice);
    s.SetString("thinking", thinking);
    s.SetBool("bin_mode", binaryAudioMode);
    s.SetString("prompt", systemPrompt);

    s.SetBool("doh_en", dohEch.dohEnabled);
    s.SetString("doh_url1", dohEch.dohUrl1);
    s.SetString("doh_url2", dohEch.dohUrl2);
    s.SetString("conn_ip", dohEch.connectIp);
}

GeminiLiveClient::GeminiLiveClient() = default;

GeminiLiveClient::~GeminiLiveClient()
{
    stop();
}

void GeminiLiveClient::start(const GeminiConfig& config)
{
    stop();
    _config                        = config;
    _running                       = true;
    _ptt_active                    = false;
    _ptt_just_started              = false;
    _ptt_just_ended                = false;
    _setup_complete                = false;
    _server_turn_finished          = true;
    _ignore_current_turn_audio     = false;
    _in_progress_continuation_count = 0;
    _state                         = SessionState::Connecting;
    setStatusText("Connecting...");

    esp_wifi_set_ps(WIFI_PS_NONE);

    xTaskCreate([](void* arg) { static_cast<GeminiLiveClient*>(arg)->taskLoop(); }, "gemini_live", 16 * 1024, this, 5,
                &_task_handle);
}

void GeminiLiveClient::stop()
{
    _running    = false;
    _ptt_active = false;
    GetHAL().audioClearStream();

    _ws.abort();

    if (_task_handle != nullptr) {
        for (int i = 0; i < 50 && _task_handle != nullptr; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (_task_handle != nullptr) {
            TaskHandle_t th = _task_handle;
            _task_handle    = nullptr;
            vTaskDelete(th);
        }
    }

    _ws.disconnect();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    _state = SessionState::Disconnected;
}

void GeminiLiveClient::setPttTalking(bool talking)
{
    bool prev = _ptt_active.exchange(talking);
    if (talking && !prev) {
        GetHAL().resetActivityTimer();
        GetHAL().audioClearStream();
        // If server is still streaming the old turn, ignore any remaining in-flight audio until interrupted/turnComplete
        _ignore_current_turn_audio = !_server_turn_finished.load();
        _ptt_just_started          = true;
        _ptt_just_ended            = false;
        if (_setup_complete.load()) {
            _state = SessionState::Listening;
            setStatusText("Listening...");
        }
    } else if (!talking && prev) {
        _ptt_just_ended = true;
        if (_setup_complete.load()) {
            _state = SessionState::Thinking;
            setStatusText("Thinking...");
        }
    }
}

void GeminiLiveClient::togglePttTalking()
{
    setPttTalking(!_ptt_active.load());
}

std::string GeminiLiveClient::getStatusText() const
{
    std::lock_guard<std::mutex> lock(_text_mutex);
    return _status_text;
}

std::string GeminiLiveClient::getUserTranscript() const
{
    std::lock_guard<std::mutex> lock(_text_mutex);
    return _user_transcript;
}

std::string GeminiLiveClient::getModelTranscript() const
{
    std::lock_guard<std::mutex> lock(_text_mutex);
    return _model_transcript;
}

std::string GeminiLiveClient::getConnectedIp() const
{
    std::lock_guard<std::mutex> lock(_text_mutex);
    return _connected_ip;
}

void GeminiLiveClient::setStatusText(const std::string& text)
{
    std::lock_guard<std::mutex> lock(_text_mutex);
    _status_text = text;
}

void trim_utf8_prefix(std::string& s, std::size_t max_bytes)
{
    if (s.size() <= max_bytes) {
        return;
    }
    std::size_t cut = s.size() - max_bytes;
    while (cut < s.size() && (static_cast<uint8_t>(s[cut]) & 0xC0) == 0x80) {
        ++cut;
    }
    s.erase(0, cut);
}

void GeminiLiveClient::appendUserTranscript(const std::string& text, bool replace)
{
    std::lock_guard<std::mutex> lock(_text_mutex);
    if (replace) {
        _user_transcript = text;
    } else {
        _user_transcript += text;
        trim_utf8_prefix(_user_transcript, 240);
    }
}

void GeminiLiveClient::appendModelTranscript(const std::string& text, bool replace)
{
    std::lock_guard<std::mutex> lock(_text_mutex);
    if (replace) {
        _model_transcript = text;
    } else {
        _model_transcript += text;
        trim_utf8_prefix(_model_transcript, 360);
    }
}

std::string GeminiLiveClient::buildSetupJson() const
{
    JsonDocument doc;
    JsonObject setup = doc["setup"].to<JsonObject>();

    std::string model = _config.model;
    if (model.empty()) {
        model = "models/gemini-3.8-live";
    } else if (model.rfind("models/", 0) != 0) {
        model = "models/" + model;
    }
    setup["model"] = model;

    JsonObject genCfg = setup["generationConfig"].to<JsonObject>();
    JsonArray modalities = genCfg["responseModalities"].to<JsonArray>();
    modalities.add("AUDIO");

    std::string voice = _config.voice.empty() ? "Leda" : _config.voice;
    genCfg["speechConfig"]["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"] = voice;

    if (model.find("extended-thinking") != std::string::npos) {
        genCfg["thinkingConfig"]["thinkingLevel"] = (_config.thinking == "HIGH") ? "HIGH" : "LOW";
    }

    setup["inputAudioTranscription"].to<JsonObject>();
    setup["outputAudioTranscription"].to<JsonObject>();

    std::string sys_text = _config.systemPrompt + "\n【当前手表实时时间】北京时间：" + get_current_datetime_str() +
                           "；电池电量：" + std::to_string(GetHAL().getBatteryLevel()) + "%。";
    setup["systemInstruction"]["parts"][0]["text"] = sys_text;

    JsonArray funcDecls = setup["tools"][0]["functionDeclarations"].to<JsonArray>();

    {
        JsonObject f     = funcDecls.add<JsonObject>();
        f["name"]        = "set_volume";
        f["description"] = "设置手表扬声器音量（0到100）";
        f["parameters"]["type"] = "object";
        f["parameters"]["properties"]["volume"]["type"]        = "integer";
        f["parameters"]["properties"]["volume"]["description"] = "音量百分比 0..100";
        f["parameters"]["required"].add("volume");
    }
    {
        JsonObject f     = funcDecls.add<JsonObject>();
        f["name"]        = "set_brightness";
        f["description"] = "设置手表屏幕亮度（10到100）";
        f["parameters"]["type"] = "object";
        f["parameters"]["properties"]["brightness"]["type"]        = "integer";
        f["parameters"]["properties"]["brightness"]["description"] = "屏幕亮度百分比 10..100";
        f["parameters"]["required"].add("brightness");
    }
    {
        JsonObject f     = funcDecls.add<JsonObject>();
        f["name"]        = "trigger_vibration";
        f["description"] = "触发手表线性马达震动提醒";
        f["parameters"]["type"] = "object";
        f["parameters"]["properties"]["duration_ms"]["type"] = "integer";
    }
    {
        JsonObject f     = funcDecls.add<JsonObject>();
        f["name"]        = "get_watch_status";
        f["description"] = "获取手表当前准确北京时间、日期、电池电量、充电状态、音量与屏幕亮度";
        f["parameters"]["type"] = "object";
        f["parameters"]["properties"].to<JsonObject>();
    }
    {
        JsonObject f     = funcDecls.add<JsonObject>();
        f["name"]        = "set_alarm";
        f["description"] = "在手表上添加或设置闹钟（24小时制 hour: 0..23, minute: 0..59）";
        f["parameters"]["type"] = "object";
        f["parameters"]["properties"]["hour"]["type"]   = "integer";
        f["parameters"]["properties"]["minute"]["type"] = "integer";
        f["parameters"]["required"].add("hour");
        f["parameters"]["required"].add("minute");
    }
    {
        JsonObject f     = funcDecls.add<JsonObject>();
        f["name"]        = "get_weather";
        f["description"] = "查询指定城市（默认北京市丰台区）的实时天气、气温、湿度、空气质量及预报";
        f["parameters"]["type"] = "object";
        f["parameters"]["properties"]["city"]["type"]        = "string";
        f["parameters"]["properties"]["city"]["description"] = "城市或区县名，如北京丰台区、上海、深圳";
    }
    {
        JsonObject f     = funcDecls.add<JsonObject>();
        f["name"]        = "stop_conversation";
        f["description"] = "当用户说再见、退下、暂停、没事了时结束当前语音会话";
        f["parameters"]["type"] = "object";
        f["parameters"]["properties"].to<JsonObject>();
    }

    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string GeminiLiveClient::executeToolCalls(const std::string& rawJsonText)
{
    JsonDocument doc;
    if (deserializeJson(doc, rawJsonText) != DeserializationError::Ok) {
        return {};
    }

    JsonArray calls = doc["toolCall"]["functionCalls"].as<JsonArray>();
    if (calls.isNull() || calls.size() == 0) {
        return {};
    }

    JsonDocument respDoc;
    JsonArray funcResps = respDoc["toolResponse"]["functionResponses"].to<JsonArray>();

    for (JsonObject call : calls) {
        std::string name = call["name"] | "";
        std::string id   = call["id"] | "";
        JsonObject args  = call["args"].as<JsonObject>();

        mclog::tagInfo(_tag, "Executing toolCall: {} (id={})", name, id);
        setStatusText("Tool: " + name);

        JsonObject item = funcResps.add<JsonObject>();
        item["name"]    = name;
        if (!id.empty()) {
            item["id"] = id;
        }
        JsonObject resObj = item["response"]["result"].to<JsonObject>();

        if (name == "set_volume") {
            int vol = args["volume"] | GetHAL().getSpeakerVolume();
            vol     = std::clamp(vol, 0, 100);
            GetHAL().setSpeakerVolume(vol, true);
            resObj["status"] = "ok";
            resObj["volume"] = vol;
        } else if (name == "set_brightness") {
            int br = args["brightness"] | GetHAL().getBackLightBrightness();
            br     = std::clamp(br, 10, 100);
            GetHAL().setBackLightBrightness(br, true);
            resObj["status"]     = "ok";
            resObj["brightness"] = br;
        } else if (name == "trigger_vibration") {
            int dur = args["duration_ms"] | 200;
            dur     = std::clamp(dur, 20, 1500);
            GetHAL().vibrate(static_cast<uint16_t>(dur), 90);
            resObj["status"]      = "ok";
            resObj["duration_ms"] = dur;
        } else if (name == "get_watch_status") {
            resObj["status"]     = "ok";
            resObj["datetime"]   = get_current_datetime_str();
            resObj["battery"]    = GetHAL().getBatteryLevel();
            resObj["charging"]   = GetHAL().isBatteryCharging();
            resObj["volume"]     = GetHAL().getSpeakerVolume();
            resObj["brightness"] = GetHAL().getBackLightBrightness();
            resObj["location"]   = "北京市丰台区";
        } else if (name == "set_alarm") {
            int h = args["hour"] | 8;
            int m = args["minute"] | 0;
            if (h >= 0 && h < 24 && m >= 0 && m < 60) {
                AlarmStorageSnapshot snap;
                GetHAL().loadAlarmStorage(snap);
                if (snap.count < AlarmStorageSnapshot::maxAlarmCount) {
                    snap.alarms[snap.count].hour    = static_cast<uint8_t>(h);
                    snap.alarms[snap.count].minute  = static_cast<uint8_t>(m);
                    snap.alarms[snap.count].enabled = 1;
                    snap.count++;
                    GetHAL().saveAlarmStorage(snap);
                    resObj["status"] = "ok";
                    char tbuf[16];
                    std::snprintf(tbuf, sizeof(tbuf), "%02d:%02d", h, m);
                    resObj["alarm_time"] = tbuf;
                } else {
                    resObj["status"]  = "error";
                    resObj["message"] = "Alarm storage full";
                }
            } else {
                resObj["status"]  = "error";
                resObj["message"] = "Invalid time";
            }
        } else if (name == "get_weather") {
            std::string city = args["city"] | "北京市丰台区";
            std::string wjson = query_weather_itboy(city);
            JsonDocument wdoc;
            if (deserializeJson(wdoc, wjson) == DeserializationError::Ok) {
                resObj.set(wdoc.as<JsonObject>());
            } else {
                resObj["status"] = "error";
            }
        } else if (name == "stop_conversation") {
            resObj["status"]  = "ok";
            resObj["message"] = "请简短向用户道别";
        } else {
            resObj["status"]  = "error";
            resObj["message"] = "Unknown tool";
        }
    }

    std::string out;
    serializeJson(respDoc, out);
    return out;
}

void GeminiLiveClient::handleServerBinaryPcm(const uint8_t* data, std::size_t len)
{
    if (data == nullptr || len < 2) {
        return;
    }

    // Check if binary frame is actually a UTF-8 JSON message (when connected in raw JSON mode)
    if (data[0] == '{') {
        handleServerJson(std::string(reinterpret_cast<const char*>(data), len));
        return;
    }

    if (_ptt_active.load() || _ignore_current_turn_audio.load()) {
        return;
    }

    _server_turn_finished = false;
    GetHAL().resetActivityTimer();
    if (_state.load() != SessionState::Speaking) {
        _state = SessionState::Speaking;
        setStatusText("Speaking...");
    }

    const auto* samples     = reinterpret_cast<const int16_t*>(data);
    const std::size_t count = len / sizeof(int16_t);
    GetHAL().audioPushStreamPcm(samples, count, 24000);
}

void GeminiLiveClient::handleServerJson(const std::string& jsonText)
{
    if (jsonText.empty()) {
        return;
    }

    // Fast path for large inlineData audio chunks in standard JSON mode without blowing up DOM memory
    std::size_t inline_pos = jsonText.find("\"inlineData\"");
    while (inline_pos != std::string::npos && !_ptt_active.load() && !_ignore_current_turn_audio.load()) {
        const std::size_t data_key = jsonText.find("\"data\"", inline_pos);
        if (data_key == std::string::npos) {
            break;
        }
        const std::size_t q1 = jsonText.find('"', data_key + 6);
        if (q1 == std::string::npos) {
            break;
        }
        const std::size_t q2 = jsonText.find('"', q1 + 1);
        if (q2 == std::string::npos || q2 <= q1 + 1) {
            break;
        }
        std::vector<uint8_t> pcm_bytes;
        if (decode_base64_bytes(jsonText.data() + q1 + 1, q2 - (q1 + 1), pcm_bytes)) {
            handleServerBinaryPcm(pcm_bytes.data(), pcm_bytes.size());
        }
        inline_pos = jsonText.find("\"inlineData\"", q2);
    }

    if (jsonText.find("\"setupComplete\"") != std::string::npos) {
        if (!_setup_complete.exchange(true)) {
            mclog::tagInfo(_tag, "setupComplete received! Gemini 3.8 Live ready.");
            _server_turn_finished      = true;
            _ignore_current_turn_audio = false;
            _state                     = SessionState::Ready;
            setStatusText("Ready - Hold/Tap to Talk");
            audio::play_tone_from_midi(72, 0.04f, 0.4f);
        }
    }

    if (jsonText.find("\"toolCall\"") != std::string::npos) {
        std::string tool_resp = executeToolCalls(jsonText);
        if (!tool_resp.empty()) {
            _in_progress_continuation_count = 0;
            _ws.sendText(tool_resp);
        }
    }

    // Parse transcripts and turn flags only if relevant keys exist to save CPU & avoid PSRAM churn
    const bool has_server_content = (jsonText.find("\"serverContent\"") != std::string::npos);
    if (has_server_content && jsonText.size() < 16384) {
        JsonDocument doc;
        if (deserializeJson(doc, jsonText) == DeserializationError::Ok) {
            JsonObject sc = doc["serverContent"].as<JsonObject>();
            if (!sc.isNull()) {
                const bool is_interrupted   = (sc["interrupted"] == true);
                const bool is_turn_complete = (sc["turnComplete"] == true);
                const bool is_gen_complete  = (sc["generationComplete"] == true);

                if (!sc["modelTurn"].isNull() && !is_turn_complete && !is_gen_complete && !is_interrupted) {
                    _server_turn_finished = false;
                }

                if (is_interrupted) {
                    _server_turn_finished           = true;
                    _ignore_current_turn_audio      = false;
                    _in_progress_continuation_count = 0;
                    GetHAL().audioClearStream();
                    if (!_ptt_active.load()) {
                        _state = SessionState::Ready;
                        setStatusText("Ready");
                    }
                }

                const char* in_tx = sc["inputTranscription"]["text"];
                if (in_tx != nullptr && in_tx[0] != '\0') {
                    appendUserTranscript(in_tx, false);
                }

                if (!_ignore_current_turn_audio.load()) {
                    const char* out_tx = sc["outputTranscription"]["text"];
                    if (out_tx != nullptr && out_tx[0] != '\0' && std::strstr(out_tx, "<no speech>") == nullptr) {
                        appendModelTranscript(out_tx, false);
                    }

                    JsonArray parts = sc["modelTurn"]["parts"].as<JsonArray>();
                    if (!parts.isNull()) {
                        for (JsonObject part : parts) {
                            if (part["thought"] == true) continue;
                            const char* t = part["text"];
                            if (t != nullptr && t[0] != '\0' && std::strstr(t, "<no speech>") == nullptr) {
                                appendModelTranscript(t, false);
                            }
                        }
                    }
                }

                if (is_turn_complete || is_gen_complete) {
                    _server_turn_finished = true;
                    const char* status    = sc["interactionStatus"];
                    const bool should_continue_stage2 =
                        is_turn_complete && status != nullptr && std::strcmp(status, "IN_PROGRESS") == 0 &&
                        _in_progress_continuation_count < 3 && !_ptt_active.load() &&
                        !_ignore_current_turn_audio.load();

                    if (should_continue_stage2) {
                        _in_progress_continuation_count++;
                        _ws.sendText(R"({"clientContent":{"turnComplete":true}})");
                    } else {
                        _ignore_current_turn_audio = false;
                    }
                }
            }
        }
    }
}

void GeminiLiveClient::taskLoop()
{
    mclog::tagInfo(_tag, "GeminiLive task started");

    while (_running.load()) {
        // Ensure Wi-Fi is connected and screen is awake
        auto wifi = GetHAL().getWifiStatus();
        if (!wifi.connected || GetHAL().isScreenSleeping()) {
            _state = SessionState::Error;
            if (GetHAL().isScreenSleeping()) {
                setStatusText("Standby (Screen Asleep)");
            } else {
                setStatusText(wifi.enabled ? "Waiting for Wi-Fi..." : "Wi-Fi is Off (Enable in Settings)");
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (!_ws.isConnected()) {
            _setup_complete = false;
            _state          = SessionState::Connecting;
            setStatusText("Connecting...");

            std::string effective_url = _config.wsUrl;
            const bool is_google = effective_url.find("generativelanguage.googleapis.com") != std::string::npos;
            if (!is_google && _config.binaryAudioMode && effective_url.find("mode=") == std::string::npos) {
                effective_url += (effective_url.find('?') != std::string::npos) ? "&mode=binary" : "?mode=binary";
            } else if (is_google && !_config.apiKey.empty() && effective_url.find("key=") == std::string::npos) {
                effective_url +=
                    (effective_url.find('?') != std::string::npos ? "&key=" : "?key=") + _config.apiKey;
            }

            std::string err_msg;
            if (!_ws.connect(effective_url, is_google ? "" : _config.wsToken, is_google ? "" : _config.apiKey,
                             _config.dohEch, err_msg)) {
                _state = SessionState::Error;
                setStatusText("Connect failed: " + err_msg);
                for (int i = 0; i < 30 && _running.load(); ++i) {
                    if (GetHAL().isScreenSleeping() || !GetHAL().isWifiConnected()) {
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(_text_mutex);
                _connected_ip = _ws.connectedIp();
            }

            esp_wifi_set_ps(WIFI_PS_NONE);

            setStatusText("Handshaking Gemini Setup...");
            std::string setup_json = buildSetupJson();
            if (!_ws.sendText(setup_json)) {
                _ws.disconnect();
                continue;
            }
        }

        // Handle PTT state transitions
        if (_ptt_just_started.exchange(false)) {
            _in_progress_continuation_count = 0;
            appendUserTranscript("", true);
            appendModelTranscript("", true);
        }

        if (_ptt_active.load() && _setup_complete.load()) {
            std::vector<int16_t> pcm16k;
            if (GetHAL().audioRecordStream16k(pcm16k, 60, 32.0f) && !pcm16k.empty()) {
                _ignore_current_turn_audio = false;
                const auto* raw_bytes      = reinterpret_cast<const uint8_t*>(pcm16k.data());
                const std::size_t nbyte    = pcm16k.size() * sizeof(int16_t);
                const bool is_google =
                    _config.wsUrl.find("generativelanguage.googleapis.com") != std::string::npos;

                if (_config.binaryAudioMode && !is_google) {
                    _ws.sendBinary(raw_bytes, nbyte);
                } else {
                    std::string b64 = encode_base64_str(raw_bytes, nbyte);
                    std::string msg =
                        R"({"realtimeInput":{"audio":{"mimeType":"audio/pcm;rate=16000","data":")" + b64 + R"("}}})";
                    _ws.sendText(msg);
                }
            }
        }

        if (_ptt_just_ended.exchange(false) && _setup_complete.load()) {
            _ignore_current_turn_audio = false;
            _ws.sendText(R"({"realtimeInput":{"audioStreamEnd":true}})");
        }

        // Flow control backpressure:
        // If the speaker already has > 10 seconds of unplayed speech queued, pause polling
        // to let the buffer drain and let TCP flow control throttle the server stream.
        if (!_ptt_active.load() && GetHAL().audioGetStreamQueuedMs() > 10000) {
            vTaskDelay(pdMS_TO_TICKS(25));
            continue;
        }

        // Drain incoming frames: use initial timeout, then 0ms to flush any queued WebSocket frames
        for (int frame_idx = 0; frame_idx < 16 && _ws.isConnected(); ++frame_idx) {
            bool got_frame = false;
            int wait_ms    = (frame_idx == 0) ? (_ptt_active.load() ? 2 : 10) : 0;
            bool ok        = _ws.poll(
                wait_ms,
                [this, &got_frame](const std::string& txt) {
                    got_frame = true;
                    handleServerJson(txt);
                },
                [this, &got_frame](const uint8_t* bin, std::size_t len) {
                    got_frame = true;
                    handleServerBinaryPcm(bin, len);
                },
                [this](const std::string& err) {
                    mclog::tagWarn(_tag, "WS error: {}", err);
                    setStatusText("Disconnected: " + err);
                    _state = SessionState::Error;
                });
            if (!ok || !got_frame) {
                break;
            }
        }

        // Update state from Speaking -> Ready once speaker buffer drains
        if (_state.load() == SessionState::Speaking && !GetHAL().isAudioPlaying() &&
            GetHAL().audioGetStreamQueuedMs() == 0) {
            _state = SessionState::Ready;
            setStatusText("Ready - Hold/Tap to Talk");
        }
    }

    _ws.disconnect();
    _task_handle = nullptr;
    mclog::tagInfo(_tag, "GeminiLive task stopped");
    vTaskDelete(nullptr);
}

}  // namespace gemini

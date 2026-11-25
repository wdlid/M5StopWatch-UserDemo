/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "weather.h"

#include <ArduinoJson.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <hal/hal.h>
#include <mooncake_log.h>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace weather {
namespace {

constexpr const char* _tag = "Weather";

// Default: Beijing Fengtai (same as Gemini weather tool)
constexpr const char* kCityCode   = "101010900";
constexpr const char* kWeatherUrl = "http://t.weather.itboy.net/api/weather/city/101010900";

constexpr uint32_t kRefreshOkMs   = 30 * 60 * 1000;  // 30 min
constexpr uint32_t kRefreshFailMs = 5 * 60 * 1000;   // 5 min retry
constexpr uint32_t kMinTickGapMs  = 2000;

struct RespBuf {
    std::string body;
};

std::string map_weather_type(const char* type_cn)
{
    if (type_cn == nullptr || type_cn[0] == '\0') {
        return {};
    }
    if (std::strstr(type_cn, "雷") != nullptr) {
        return "Storm";
    }
    if (std::strstr(type_cn, "雪") != nullptr) {
        return "Snow";
    }
    if (std::strstr(type_cn, "雨") != nullptr) {
        return "Rain";
    }
    if (std::strstr(type_cn, "雹") != nullptr) {
        return "Hail";
    }
    if (std::strstr(type_cn, "雾") != nullptr) {
        return "Fog";
    }
    if (std::strstr(type_cn, "霾") != nullptr) {
        return "Haze";
    }
    if (std::strstr(type_cn, "沙") != nullptr || std::strstr(type_cn, "尘") != nullptr) {
        return "Dust";
    }
    if (std::strstr(type_cn, "阴") != nullptr) {
        return "Overcast";
    }
    if (std::strstr(type_cn, "云") != nullptr) {
        return "Cloudy";
    }
    if (std::strstr(type_cn, "晴") != nullptr) {
        return "Clear";
    }
    return {};
}

AqiLevel level_from_quality(const char* quality)
{
    if (quality == nullptr || quality[0] == '\0') {
        return AqiLevel::Unknown;
    }
    if (std::strstr(quality, "严重") != nullptr) {
        return AqiLevel::Hazardous;
    }
    if (std::strstr(quality, "重度") != nullptr) {
        return AqiLevel::VeryUnhealthy;
    }
    if (std::strstr(quality, "中度") != nullptr) {
        return AqiLevel::Unhealthy;
    }
    if (std::strstr(quality, "轻度") != nullptr) {
        return AqiLevel::UnhealthySg;
    }
    if (std::strstr(quality, "良") != nullptr) {
        return AqiLevel::Moderate;
    }
    if (std::strstr(quality, "优") != nullptr) {
        return AqiLevel::Good;
    }
    return AqiLevel::Unknown;
}

AqiLevel level_from_pm25(int pm25)
{
    if (pm25 < 0) {
        return AqiLevel::Unknown;
    }
    if (pm25 <= 35) {
        return AqiLevel::Good;
    }
    if (pm25 <= 75) {
        return AqiLevel::Moderate;
    }
    if (pm25 <= 115) {
        return AqiLevel::UnhealthySg;
    }
    if (pm25 <= 150) {
        return AqiLevel::Unhealthy;
    }
    if (pm25 <= 250) {
        return AqiLevel::VeryUnhealthy;
    }
    return AqiLevel::Hazardous;
}

std::string map_wind_dir(const char* fx)
{
    if (fx == nullptr || fx[0] == '\0') {
        return {};
    }
    if (std::strstr(fx, "东北") != nullptr) {
        return "NE";
    }
    if (std::strstr(fx, "东南") != nullptr) {
        return "SE";
    }
    if (std::strstr(fx, "西北") != nullptr) {
        return "NW";
    }
    if (std::strstr(fx, "西南") != nullptr) {
        return "SW";
    }
    if (std::strstr(fx, "东") != nullptr) {
        return "E";
    }
    if (std::strstr(fx, "南") != nullptr) {
        return "S";
    }
    if (std::strstr(fx, "西") != nullptr) {
        return "W";
    }
    if (std::strstr(fx, "北") != nullptr) {
        return "N";
    }
    if (std::strstr(fx, "无") != nullptr || std::strstr(fx, "微") != nullptr) {
        return "Calm";
    }
    return {};
}

std::string extract_wind_level(const char* fl)
{
    if (fl == nullptr || fl[0] == '\0') {
        return {};
    }
    std::string digits;
    for (const char* p = fl; *p; ++p) {
        if ((*p >= '0' && *p <= '9') || (*p == '-' && !digits.empty())) {
            digits.push_back(*p);
        }
    }
    if (digits.empty()) {
        return {};
    }
    return "Lv." + digits;
}

bool parse_temp_celsius(const char* wendu, int& out_c)
{
    if (wendu == nullptr || wendu[0] == '\0') {
        return false;
    }
    char* endptr = nullptr;
    float val    = std::strtof(wendu, &endptr);
    if (endptr == wendu || !std::isfinite(val)) {
        return false;
    }
    out_c = static_cast<int>(std::lround(val));
    return true;
}

esp_err_t http_event_cb(esp_http_client_event_t* evt)
{
    auto* buf = static_cast<RespBuf*>(evt->user_data);
    if (evt->event_id == HTTP_EVENT_ON_DATA && buf != nullptr && evt->data != nullptr && evt->data_len > 0) {
        if (buf->body.size() + static_cast<size_t>(evt->data_len) <= 8192) {
            buf->body.append(static_cast<const char*>(evt->data), evt->data_len);
        }
    }
    return ESP_OK;
}

SemaphoreHandle_t g_mutex = nullptr;
Snapshot g_snapshot;
std::atomic<bool> g_fetching{false};
std::atomic<uint32_t> g_next_fetch_ms{0};
uint32_t g_last_tick_ms = 0;

void ensure_mutex()
{
    if (g_mutex == nullptr) {
        g_mutex = xSemaphoreCreateMutex();
    }
}

void set_snapshot(Snapshot snap)
{
    ensure_mutex();
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_snapshot = std::move(snap);
        xSemaphoreGive(g_mutex);
    }
}

bool fetch_once()
{
    RespBuf resp;
    esp_http_client_config_t cfg = {};
    cfg.url                      = kWeatherUrl;
    cfg.method                   = HTTP_METHOD_GET;
    cfg.timeout_ms               = 4000;
    cfg.event_handler            = http_event_cb;
    cfg.user_data                = &resp;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        mclog::tagWarn(_tag, "http client init failed");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || resp.body.empty()) {
        mclog::tagWarn(_tag, "fetch failed err={} status={}", static_cast<int>(err), status);
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp.body) != DeserializationError::Ok) {
        mclog::tagWarn(_tag, "json parse failed");
        return false;
    }

    const char* wendu   = doc["data"]["wendu"] | "";
    const char* quality = doc["data"]["quality"] | "";
    const char* shidu   = doc["data"]["shidu"] | "";
    int pm25            = doc["data"]["pm25"] | -1;

    const char* type_cn = "";
    const char* fx_cn   = "";
    const char* fl_cn   = "";
    int forecast_aqi    = -1;
    if (doc["data"]["forecast"].is<JsonArray>() && doc["data"]["forecast"].size() > 0) {
        JsonVariantConst today = doc["data"]["forecast"][0];
        type_cn                = today["type"] | "";
        fx_cn                  = today["fx"] | "";
        fl_cn                  = today["fl"] | "";
        forecast_aqi           = today["aqi"] | -1;
    }

    Snapshot snap;
    if (parse_temp_celsius(wendu, snap.tempC)) {
        snap.hasTemp     = true;
        snap.temperature = std::to_string(snap.tempC) + "C";
        snap.tempDisplay = std::to_string(snap.tempC) + "\xC2\xB0" "C";
    }
    snap.condition = map_weather_type(type_cn);
    snap.windDir   = map_wind_dir(fx_cn);
    snap.windLevel = extract_wind_level(fl_cn);
    if (!snap.windDir.empty() && !snap.windLevel.empty()) {
        snap.windText = (snap.windDir == "Calm") ? snap.windDir : (snap.windDir + " " + snap.windLevel);
    } else if (!snap.windDir.empty()) {
        snap.windText = snap.windDir;
    } else if (!snap.windLevel.empty()) {
        snap.windText = snap.windLevel;
    }
    if (shidu != nullptr && shidu[0] != '\0') {
        snap.humidity = shidu;
    }

    const int aqi = (forecast_aqi >= 0) ? forecast_aqi : pm25;
    snap.aqiValue = aqi;
    snap.aqiLevel = level_from_quality(quality);
    if (snap.aqiLevel == AqiLevel::Unknown) {
        snap.aqiLevel = level_from_pm25(aqi);
    }
    if (aqi >= 0) {
        snap.aqiText = "AQ" + std::to_string(aqi);
    }

    if (snap.temperature.empty() && snap.condition.empty() && snap.aqiText.empty() && snap.windText.empty()) {
        mclog::tagWarn(_tag, "empty weather snapshot");
        return false;
    }

    snap.valid = true;
    mclog::tagInfo(_tag, "updated: {} {} wind='{}' {} (city {})", snap.temperature, snap.condition, snap.windText,
                   snap.aqiText, kCityCode);
    set_snapshot(std::move(snap));
    return true;
}

void fetch_task(void* /*arg*/)
{
    const bool ok      = fetch_once();
    const uint32_t now = GetHAL().millis();
    g_next_fetch_ms.store(now + (ok ? kRefreshOkMs : kRefreshFailMs), std::memory_order_release);
    g_fetching.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}

}  // namespace

void tick()
{
    const uint32_t now = GetHAL().millis();
    if (now - g_last_tick_ms < kMinTickGapMs) {
        return;
    }
    g_last_tick_ms = now;

    if (!GetHAL().isWifiConnected()) {
        return;
    }
    if (g_fetching.load(std::memory_order_acquire)) {
        return;
    }
    if (now < g_next_fetch_ms.load(std::memory_order_acquire)) {
        return;
    }

    g_fetching.store(true, std::memory_order_release);
    BaseType_t ok = xTaskCreate(fetch_task, "weather_fetch", 6 * 1024, nullptr, 3, nullptr);
    if (ok != pdPASS) {
        g_fetching.store(false, std::memory_order_release);
        g_next_fetch_ms.store(now + kRefreshFailMs, std::memory_order_release);
        mclog::tagWarn(_tag, "failed to create fetch task");
    }
}

Snapshot snapshot()
{
    ensure_mutex();
    Snapshot out;
    if (g_mutex && xSemaphoreTake(g_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        out = g_snapshot;
        xSemaphoreGive(g_mutex);
    }
    return out;
}

}  // namespace weather

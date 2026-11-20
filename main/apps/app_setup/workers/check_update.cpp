/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <apps/common/common.h>
#include <apps/common/key_manager/key_manager.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <uitk/short_namespace.hpp>

#include <ArduinoJson.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace setup_workers;

namespace {

constexpr const char* _tag = "Setup-Update";
constexpr const char* GITHUB_RELEASES_URL =
    "https://api.github.com/repos/wdlid/M5StopWatch-UserDemo/releases?per_page=5";
constexpr const char* GITHUB_LATEST_URL =
    "https://api.github.com/repos/wdlid/M5StopWatch-UserDemo/releases/latest";

std::string format_bytes(std::size_t bytes)
{
    if (bytes == 0) return "0 B";
    char buf[32];
    if (bytes >= 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%u B", static_cast<unsigned int>(bytes));
    }
    return std::string(buf);
}

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    static Version parse(std::string_view str)
    {
        Version v;
        size_t i = 0;
        while (i < str.size() && (str[i] == ' ' || str[i] == 'v' || str[i] == 'V')) {
            i++;
        }
        int parts[3]   = {0, 0, 0};
        int part_idx   = 0;
        bool has_digit = false;
        while (i < str.size() && part_idx < 3) {
            if (std::isdigit(static_cast<unsigned char>(str[i]))) {
                parts[part_idx] = parts[part_idx] * 10 + (str[i] - '0');
                has_digit       = true;
            } else if (str[i] == '.') {
                part_idx++;
            } else {
                break;
            }
            i++;
        }
        if (has_digit) {
            v.major = parts[0];
            v.minor = parts[1];
            v.patch = parts[2];
        }
        return v;
    }

    bool operator>(const Version& o) const
    {
        if (major != o.major) return major > o.major;
        if (minor != o.minor) return minor > o.minor;
        return patch > o.patch;
    }

    bool operator<=(const Version& o) const
    {
        return !(*this > o);
    }
};

struct HttpRespBuf {
    std::string body;
    size_t max_size                = 64 * 1024;
    FILE* file                     = nullptr;
    size_t expected_size           = 0;
    size_t received_bytes          = 0;
    size_t total_bytes             = 0;
    int last_progress              = -1;
    bool write_error               = false;
    std::atomic<bool>* abort_flag = nullptr;
    std::function<void(size_t received, size_t total, int progress)> on_progress;
};

esp_err_t http_event_cb(esp_http_client_event_t* evt)
{
    auto* b = static_cast<HttpRespBuf*>(evt->user_data);
    if (b != nullptr && b->abort_flag != nullptr && b->abort_flag->load()) {
        return ESP_FAIL;
    }
    if (evt->event_id == HTTP_EVENT_ON_DATA && b != nullptr && evt->data != nullptr && evt->data_len > 0) {
        if (b->file != nullptr) {
            int status = esp_http_client_get_status_code(evt->client);
            if (status >= 200 && status < 300) {
                if (b->total_bytes == 0) {
                    int64_t cl = esp_http_client_get_content_length(evt->client);
                    b->total_bytes = (cl > 0) ? static_cast<size_t>(cl) : b->expected_size;
                }
                size_t written = fwrite(evt->data, 1, evt->data_len, b->file);
                if (written != static_cast<size_t>(evt->data_len)) {
                    b->write_error = true;
                    return ESP_FAIL;
                }
                b->received_bytes += written;
                int progress = (b->total_bytes > 0)
                                   ? static_cast<int>((static_cast<uint64_t>(b->received_bytes) * 100) / b->total_bytes)
                                   : 0;
                if (progress > 100) progress = 100;
                if (progress != b->last_progress || (b->total_bytes > 0 && b->received_bytes >= b->total_bytes)) {
                    b->last_progress = progress;
                    if (b->on_progress) {
                        b->on_progress(b->received_bytes, b->total_bytes, progress);
                    }
                }
            }
        } else if (b->body.size() + evt->data_len <= b->max_size) {
            b->body.append(static_cast<const char*>(evt->data), evt->data_len);
        }
    }
    return ESP_OK;
}

}  // namespace

struct CheckUpdateWorker::CheckTaskContext {
    std::mutex mutex;
    std::atomic<bool> abort_requested{false};
    std::atomic<bool> finished{false};
    Step result_step = Step::CheckingGithub;
    UpdateInfo info;
};

struct CheckUpdateWorker::OtaTaskContext {
    std::mutex mutex;
    std::atomic<bool> abort_requested{false};
    std::atomic<bool> finished{false};
    std::string download_url;
    size_t expected_size = 0;
    Step result_step     = Step::Downloading;
    UpdateInfo info;
};

class CheckUpdateWorker::CheckUpdateView {
public:
    CheckUpdateView()
    {
        _panel = std::make_unique<Container>(lv_screen_active());
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setSize(466, 466);
        _panel->setRadius(0);
        _panel->setBorderWidth(0);
        _panel->setPaddingAll(0);
        _panel->setBgColor(lv_color_hex(0x0F172A));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _title_label = std::make_unique<Label>(_panel->get());
        _title_label->setText("Check Update");
        _title_label->setTextFont(&MontserratSemiBold26);
        _title_label->setTextColor(lv_color_hex(0xFFFFFF));
        _title_label->align(LV_ALIGN_TOP_MID, 0, 42);

        _ver_label = std::make_unique<Label>(_panel->get());
        _ver_label->setTextFont(&lv_font_montserrat_18);
        _ver_label->setTextColor(lv_color_hex(0x94A3B8));
        _ver_label->align(LV_ALIGN_TOP_MID, 0, 78);
        _ver_label->setText(fmt::format("Current: {}", common::FirmwareVersion));

        _info_label = std::make_unique<Label>(_panel->get());
        _info_label->setTextFont(&lv_font_montserrat_22);
        _info_label->setTextColor(lv_color_hex(0xF8FAFC));
        _info_label->setWidth(380);
        _info_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _info_label->setLongMode(LV_LABEL_LONG_MODE_WRAP);
        _info_label->align(LV_ALIGN_CENTER, 0, -20);
        _info_label->setText("Checking Wi-Fi...");

        _detail_label = std::make_unique<Label>(_panel->get());
        _detail_label->setTextFont(&lv_font_montserrat_18);
        _detail_label->setTextColor(lv_color_hex(0x94A3B8));
        _detail_label->setWidth(380);
        _detail_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _detail_label->setLongMode(LV_LABEL_LONG_MODE_WRAP);
        _detail_label->align(LV_ALIGN_CENTER, 0, 20);
        _detail_label->setText("");

        // Progress bar
        _bar_obj = lv_bar_create(_panel->get());
        lv_obj_align(_bar_obj, LV_ALIGN_CENTER, 0, 65);
        lv_obj_set_size(_bar_obj, 320, 16);
        lv_bar_set_range(_bar_obj, 0, 100);
        lv_bar_set_value(_bar_obj, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_bar_obj, lv_color_hex(0x1E293B), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_bar_obj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(_bar_obj, 8, LV_PART_MAIN);
        lv_obj_set_style_bg_color(_bar_obj, lv_color_hex(0x4AD78C), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(_bar_obj, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(_bar_obj, 8, LV_PART_INDICATOR);
        lv_obj_add_flag(_bar_obj, LV_OBJ_FLAG_HIDDEN);

        // Progress detail text
        _progress_label = std::make_unique<Label>(_panel->get());
        _progress_label->setTextFont(&lv_font_montserrat_18);
        _progress_label->setTextColor(lv_color_hex(0x38BDF8));
        _progress_label->align(LV_ALIGN_CENTER, 0, 92);
        _progress_label->setText("");
        lv_obj_add_flag(_progress_label->get(), LV_OBJ_FLAG_HIDDEN);

        // Center Single Button
        _btn_center = std::make_unique<Button>(_panel->get());
        _btn_center->align(LV_ALIGN_CENTER, 0, 155);
        _btn_center->setSize(240, 54);
        _btn_center->setRadius(27);
        _btn_center->setBorderWidth(0);
        _btn_center->setShadowWidth(0);
        _btn_center->setBgColor(lv_color_hex(0x334155));
        _btn_center->label().setText("Back");
        _btn_center->label().setTextFont(&lv_font_montserrat_22);
        _btn_center->label().setTextColor(lv_color_hex(0xF8FAFC));
        _btn_center->label().align(LV_ALIGN_CENTER, 0, 0);
        _btn_center->label().removeFlag(LV_OBJ_FLAG_CLICKABLE);

        _created_at = GetHAL().millis();
        _btn_center->onClick().connect([this]() {
            if (GetHAL().millis() - _created_at >= 300) {
                _center_clicked = true;
            }
        });

        // Two Action Buttons: Left ("Later")
        _btn_left = std::make_unique<Button>(_panel->get());
        _btn_left->align(LV_ALIGN_CENTER, -85, 155);
        _btn_left->setSize(140, 54);
        _btn_left->setRadius(27);
        _btn_left->setBorderWidth(0);
        _btn_left->setShadowWidth(0);
        _btn_left->setBgColor(lv_color_hex(0x334155));
        _btn_left->label().setText("Later");
        _btn_left->label().setTextFont(&lv_font_montserrat_22);
        _btn_left->label().setTextColor(lv_color_hex(0x94A3B8));
        _btn_left->label().align(LV_ALIGN_CENTER, 0, 0);
        _btn_left->label().removeFlag(LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(_btn_left->get(), LV_OBJ_FLAG_HIDDEN);

        _btn_left->onClick().connect([this]() {
            if (GetHAL().millis() - _created_at >= 300) {
                _later_clicked = true;
            }
        });

        // Two Action Buttons: Right ("Update")
        _btn_right = std::make_unique<Button>(_panel->get());
        _btn_right->align(LV_ALIGN_CENTER, 85, 155);
        _btn_right->setSize(140, 54);
        _btn_right->setRadius(27);
        _btn_right->setBorderWidth(0);
        _btn_right->setShadowWidth(0);
        _btn_right->setBgColor(lv_color_hex(0x10B981));
        _btn_right->label().setText("Update");
        _btn_right->label().setTextFont(&lv_font_montserrat_22);
        _btn_right->label().setTextColor(lv_color_hex(0xFFFFFF));
        _btn_right->label().align(LV_ALIGN_CENTER, 0, 0);
        _btn_right->label().removeFlag(LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(_btn_right->get(), LV_OBJ_FLAG_HIDDEN);

        _btn_right->onClick().connect([this]() {
            if (GetHAL().millis() - _created_at >= 300) {
                _update_clicked = true;
            }
        });
    }

    void setInfo(const std::string& info, const std::string& detail = "")
    {
        _info_label->setText(info);
        _detail_label->setText(detail);
    }

    void setProgress(int progress, size_t received, size_t total)
    {
        lv_bar_set_value(_bar_obj, progress, LV_ANIM_OFF);
        char buf[64];
        if (total > 0) {
            snprintf(buf, sizeof(buf), "%d%% (%s / %s)", progress, format_bytes(received).c_str(),
                     format_bytes(total).c_str());
        } else {
            snprintf(buf, sizeof(buf), "%s", format_bytes(received).c_str());
        }
        _progress_label->setText(buf);
    }

    void showProgressBar(bool show)
    {
        if (show) {
            lv_obj_remove_flag(_bar_obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(_progress_label->get(), LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_bar_obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(_progress_label->get(), LV_OBJ_FLAG_HIDDEN);
        }
    }

    void showSingleButton(const std::string& labelText)
    {
        _btn_center->label().setText(labelText);
        lv_obj_remove_flag(_btn_center->get(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_btn_left->get(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_btn_right->get(), LV_OBJ_FLAG_HIDDEN);
    }

    void showActionButtons(const std::string& leftText = "Later", const std::string& rightText = "Update")
    {
        lv_obj_add_flag(_btn_center->get(), LV_OBJ_FLAG_HIDDEN);
        _btn_left->label().setText(leftText);
        _btn_right->label().setText(rightText);
        lv_obj_remove_flag(_btn_left->get(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(_btn_right->get(), LV_OBJ_FLAG_HIDDEN);
    }

    void hideAllButtons()
    {
        lv_obj_add_flag(_btn_center->get(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_btn_left->get(), LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_btn_right->get(), LV_OBJ_FLAG_HIDDEN);
    }

    bool consumeCenterClicked()
    {
        bool clicked = _center_clicked;
        _center_clicked = false;
        return clicked;
    }

    bool consumeLaterClicked()
    {
        bool clicked = _later_clicked;
        _later_clicked = false;
        return clicked;
    }

    bool consumeUpdateClicked()
    {
        bool clicked = _update_clicked;
        _update_clicked = false;
        return clicked;
    }

private:
    std::unique_ptr<Container> _panel;
    std::unique_ptr<Label> _title_label;
    std::unique_ptr<Label> _ver_label;
    std::unique_ptr<Label> _info_label;
    std::unique_ptr<Label> _detail_label;
    lv_obj_t* _bar_obj = nullptr;
    std::unique_ptr<Label> _progress_label;

    std::unique_ptr<Button> _btn_center;
    std::unique_ptr<Button> _btn_left;
    std::unique_ptr<Button> _btn_right;

    uint32_t _created_at = 0;
    bool _center_clicked = false;
    bool _later_clicked  = false;
    bool _update_clicked = false;
};

CheckUpdateWorker::CheckUpdateWorker()
{
    mclog::tagInfo(_tag, "start check update worker");
    _created_at = GetHAL().millis();
    _view       = std::make_unique<CheckUpdateView>();
}

CheckUpdateWorker::~CheckUpdateWorker()
{
    mclog::tagInfo(_tag, "exit check update worker");
    _stopTask();
}

void CheckUpdateWorker::_stopTask()
{
    if (_check_ctx) {
        _check_ctx->abort_requested.store(true);
        _check_ctx.reset();
    }
    if (_ota_ctx) {
        _ota_ctx->abort_requested.store(true);
        _ota_ctx.reset();
    }
}

void CheckUpdateWorker::_startCheckTask()
{
    if (_check_ctx) {
        _check_ctx->abort_requested.store(true);
    }
    _check_ctx       = std::make_shared<CheckTaskContext>();
    _check_started_at = GetHAL().millis();

    auto* ctx_holder = new std::shared_ptr<CheckTaskContext>(_check_ctx);
    BaseType_t res   = xTaskCreate(
        [](void* arg) {
            auto* holder = static_cast<std::shared_ptr<CheckTaskContext>*>(arg);
            std::shared_ptr<CheckTaskContext> ctx = std::move(*holder);
            delete holder;

            HttpRespBuf resp;
            resp.abort_flag                 = &ctx->abort_requested;
            esp_http_client_config_t config = {};
            config.url                      = GITHUB_LATEST_URL;
            config.method                   = HTTP_METHOD_GET;
            config.crt_bundle_attach        = esp_crt_bundle_attach;
            config.timeout_ms               = 6000;
            config.buffer_size              = 4096;
            config.buffer_size_tx           = 1024;
            config.max_redirection_count    = 5;
            config.event_handler            = http_event_cb;
            config.user_data                = &resp;

            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (!client) {
                std::lock_guard<std::mutex> lk(ctx->mutex);
                ctx->result_step   = Step::CheckFailed;
                ctx->info.errorMsg = "HTTP client init failed";
                ctx->finished.store(true);
                vTaskDelete(NULL);
                return;
            }

            esp_http_client_set_header(client, "User-Agent", "M5StopWatch");
            esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");

            esp_err_t err = esp_http_client_perform(client);
            int status    = esp_http_client_get_status_code(client);
            esp_http_client_cleanup(client);

            // If /releases/latest returned 404 (e.g. only prereleases exist), fallback to /releases
            if (err == ESP_OK && status == 404 && !ctx->abort_requested.load()) {
                resp.body.clear();
                config.url = GITHUB_RELEASES_URL;
                client     = esp_http_client_init(&config);
                if (client) {
                    esp_http_client_set_header(client, "User-Agent", "M5StopWatch");
                    esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");
                    err    = esp_http_client_perform(client);
                    status = esp_http_client_get_status_code(client);
                    esp_http_client_cleanup(client);
                }
            }

            if (ctx->abort_requested.load()) {
                ctx->finished.store(true);
                vTaskDelete(NULL);
                return;
            }

            if (err != ESP_OK || status != 200 || resp.body.empty()) {
                std::lock_guard<std::mutex> lk(ctx->mutex);
                ctx->result_step = Step::CheckFailed;
                char err_buf[64];
                if (err != ESP_OK) {
                    snprintf(err_buf, sizeof(err_buf), "Network error (%s)", esp_err_to_name(err));
                } else {
                    snprintf(err_buf, sizeof(err_buf), "Server error (HTTP %d)", status);
                }
                ctx->info.errorMsg = err_buf;
                ctx->finished.store(true);
                vTaskDelete(NULL);
                return;
            }

            // Parse response JSON
            JsonDocument doc;
            DeserializationError d_err = deserializeJson(doc, resp.body);
            if (d_err != DeserializationError::Ok) {
                std::lock_guard<std::mutex> lk(ctx->mutex);
                ctx->result_step   = Step::CheckFailed;
                ctx->info.errorMsg = "Failed to parse release info";
                ctx->finished.store(true);
                vTaskDelete(NULL);
                return;
            }

            Version current_ver   = Version::parse(common::FirmwareVersion);
            Version highest_found = current_ver;
            std::string best_version;
            std::string best_url;
            size_t best_size = 0;

            auto process_release = [&](JsonObject rel) {
                const char* tag  = rel["tag_name"] | "";
                const char* name = rel["name"] | "";

                Version rel_ver = Version::parse(tag);
                if (rel_ver <= current_ver && name[0] != '\0') {
                    rel_ver = Version::parse(name);
                }

                if (rel_ver > highest_found) {
                    JsonArray assets = rel["assets"].as<JsonArray>();
                    std::string exact_url;
                    size_t exact_size = 0;
                    std::string fallback_url;
                    size_t fallback_size = 0;

                    for (JsonObject asset : assets) {
                        const char* asset_name = asset["name"] | "";
                        const char* asset_url  = asset["browser_download_url"] | "";
                        size_t asset_size      = asset["size"] | 0;
                        if (asset_url[0] == '\0') {
                            continue;
                        }

                        if (strcmp(asset_name, "firmware-ota.bin") == 0 || strcmp(asset_name, "firmware.bin") == 0) {
                            exact_url  = asset_url;
                            exact_size = asset_size;
                            break;
                        }

                        size_t name_len = strlen(asset_name);
                        if (name_len > 4 && strcmp(asset_name + name_len - 4, ".bin") == 0 &&
                            strcmp(asset_name, "bootloader.bin") != 0 && strcmp(asset_name, "partitions.bin") != 0 &&
                            strcmp(asset_name, "ota_data_initial.bin") != 0 &&
                            strstr(asset_name, "factory") == nullptr && fallback_url.empty()) {
                            fallback_url  = asset_url;
                            fallback_size = asset_size;
                        }
                    }

                    const std::string& chosen_url = !exact_url.empty() ? exact_url : fallback_url;
                    size_t chosen_size            = !exact_url.empty() ? exact_size : fallback_size;
                    if (!chosen_url.empty()) {
                        highest_found = rel_ver;
                        best_version  = (tag[0] != '\0') ? tag : name;
                        best_url      = chosen_url;
                        best_size     = chosen_size;
                    }
                }
            };

            if (doc.is<JsonArray>()) {
                for (JsonObject rel : doc.as<JsonArray>()) {
                    process_release(rel);
                }
            } else if (doc.is<JsonObject>()) {
                process_release(doc.as<JsonObject>());
            }

            {
                std::lock_guard<std::mutex> lk(ctx->mutex);
                if (highest_found > current_ver && !best_url.empty()) {
                    ctx->result_step        = Step::UpdateAvailable;
                    ctx->info.latestVersion = best_version;
                    ctx->info.downloadUrl   = best_url;
                    ctx->info.fileSize      = best_size;
                } else {
                    ctx->result_step = Step::UpToDate;
                }
            }

            ctx->finished.store(true);
            vTaskDelete(NULL);
        },
        "check_update_task", 16 * 1024, ctx_holder, 5, nullptr);

    if (res != pdPASS) {
        delete ctx_holder;
        _check_ctx.reset();
        _step          = Step::CheckFailed;
        _info.errorMsg = "Failed to start check task";
    }
}

void CheckUpdateWorker::_startOtaTask()
{
    if (_ota_ctx) {
        _ota_ctx->abort_requested.store(true);
    }
    _ota_ctx                = std::make_shared<OtaTaskContext>();
    _ota_ctx->download_url  = _info.downloadUrl;
    _ota_ctx->expected_size = _info.fileSize;
    _ota_ctx->info          = _info;

    auto* ctx_holder = new std::shared_ptr<OtaTaskContext>(_ota_ctx);
    BaseType_t res   = xTaskCreate(
        [](void* arg) {
            auto* holder = static_cast<std::shared_ptr<OtaTaskContext>*>(arg);
            std::shared_ptr<OtaTaskContext> ctx = std::move(*holder);
            delete holder;

            const std::string& url     = ctx->download_url;
            const size_t expected_size = ctx->expected_size;

            mclog::tagInfo(_tag, "Starting HTTPS OTA download to storage: {}", url);

            constexpr const char* UPDATE_PATH = "/spiflash/update.bin";
            FILE* f = fopen(UPDATE_PATH, "wb");
            if (!f) {
                mclog::tagError(_tag, "Failed to open {} for writing: {}", UPDATE_PATH, strerror(errno));
                std::lock_guard<std::mutex> lk(ctx->mutex);
                ctx->result_step   = Step::UpdateFailed;
                ctx->info.errorMsg = "Storage write error";
                ctx->finished.store(true);
                vTaskDelete(NULL);
                return;
            }

            HttpRespBuf resp;
            resp.file          = f;
            resp.expected_size = expected_size;
            resp.abort_flag    = &ctx->abort_requested;
            resp.on_progress   = [&ctx](size_t received, size_t total, int progress) {
                std::lock_guard<std::mutex> lk(ctx->mutex);
                ctx->info.bytesReceived = received;
                ctx->info.totalBytes    = total;
                ctx->info.progress      = progress;
            };

            esp_http_client_config_t http_config = {};
            http_config.url                      = url.c_str();
            http_config.method                   = HTTP_METHOD_GET;
            http_config.crt_bundle_attach        = esp_crt_bundle_attach;
            http_config.timeout_ms               = 20000;
            http_config.keep_alive_enable        = true;
            http_config.buffer_size              = 8192;
            http_config.buffer_size_tx           = 2048;
            http_config.max_redirection_count    = 5;
            http_config.user_agent               = "M5StopWatch";
            http_config.event_handler            = http_event_cb;
            http_config.user_data                = &resp;

            esp_http_client_handle_t client = esp_http_client_init(&http_config);
            if (!client) {
                fclose(f);
                remove(UPDATE_PATH);
                mclog::tagError(_tag, "esp_http_client_init failed");
                std::lock_guard<std::mutex> lk(ctx->mutex);
                ctx->result_step   = Step::UpdateFailed;
                ctx->info.errorMsg = "HTTP client error";
                ctx->finished.store(true);
                vTaskDelete(NULL);
                return;
            }

            esp_err_t err = esp_http_client_perform(client);
            int status    = esp_http_client_get_status_code(client);

            fclose(f);
            esp_http_client_cleanup(client);

            if (ctx->abort_requested.load()) {
                mclog::tagWarn(_tag, "OTA aborted by user");
                remove(UPDATE_PATH);
                ctx->finished.store(true);
                vTaskDelete(NULL);
                return;
            }

            size_t read_total = resp.received_bytes;
            mclog::tagInfo(_tag, "Download finished: err={}, status={}, bytes={}/{}", esp_err_to_name(err), status,
                           read_total, resp.total_bytes);

            if (err == ESP_OK && status == 200 && !resp.write_error && read_total > 100 * 1024 &&
                (resp.total_bytes == 0 || read_total == resp.total_bytes)) {
                // Verify magic byte (0xE9)
                FILE* check_f = fopen(UPDATE_PATH, "rb");
                uint8_t magic = 0;
                if (check_f) {
                    fread(&magic, 1, 1, check_f);
                    fclose(check_f);
                }
                if (magic != 0xE9) {
                    mclog::tagError(_tag, "Invalid firmware magic byte: 0x{:02X}", magic);
                    remove(UPDATE_PATH);
                    std::lock_guard<std::mutex> lk(ctx->mutex);
                    ctx->result_step   = Step::UpdateFailed;
                    ctx->info.errorMsg = "Invalid firmware file";
                    ctx->finished.store(true);
                    vTaskDelete(NULL);
                    return;
                }

                // Staged update successfully downloaded! Set boot partition to ota_1 (Recovery)
                const esp_partition_t* ota_1 =
                    esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
                if (ota_1) {
                    esp_err_t set_boot_err = esp_ota_set_boot_partition(ota_1);
                    if (set_boot_err == ESP_OK) {
                        mclog::tagInfo(_tag, "Update staged. Boot partition set to ota_1 (Recovery)");
                        std::lock_guard<std::mutex> lk(ctx->mutex);
                        ctx->result_step   = Step::UpdateSuccess;
                        ctx->info.progress = 100;
                        ctx->finished.store(true);
                        vTaskDelete(NULL);
                        return;
                    } else {
                        mclog::tagError(_tag, "Failed to set boot partition: {}", esp_err_to_name(set_boot_err));
                    }
                } else {
                    mclog::tagError(_tag, "ota_1 partition not found");
                }
            }

            remove(UPDATE_PATH);
            {
                std::lock_guard<std::mutex> lk(ctx->mutex);
                ctx->result_step = Step::UpdateFailed;
                if (resp.write_error) {
                    ctx->info.errorMsg = "Storage write error";
                } else if (err != ESP_OK) {
                    char err_buf[64];
                    snprintf(err_buf, sizeof(err_buf), "Download error (%s)", esp_err_to_name(err));
                    ctx->info.errorMsg = err_buf;
                } else if (status != 200) {
                    char err_buf[64];
                    snprintf(err_buf, sizeof(err_buf), "HTTP error (%d)", status);
                    ctx->info.errorMsg = err_buf;
                } else {
                    ctx->info.errorMsg = "Download incomplete";
                }
            }
            ctx->finished.store(true);
            vTaskDelete(NULL);
        },
        "ota_download_task", 16 * 1024, ctx_holder, 5, nullptr);

    if (res != pdPASS) {
        delete ctx_holder;
        _ota_ctx.reset();
        _step          = Step::UpdateFailed;
        _info.errorMsg = "Failed to start OTA task";
    }
}

void CheckUpdateWorker::update()
{
    if (!_view) return;

    if (_step == Step::CheckingGithub && _check_ctx) {
        if (_check_ctx->finished.load()) {
            std::lock_guard<std::mutex> lk(_check_ctx->mutex);
            _step = _check_ctx->result_step;
            _info = _check_ctx->info;
            _check_ctx.reset();
        } else if (GetHAL().millis() - _check_started_at >= 15000) {
            _check_ctx->abort_requested.store(true);
            _check_ctx.reset();
            _step          = Step::CheckFailed;
            _info.errorMsg = "Connection timed out";
        }
    } else if (_step == Step::Downloading && _ota_ctx) {
        std::lock_guard<std::mutex> lk(_ota_ctx->mutex);
        _info = _ota_ctx->info;
        if (_ota_ctx->finished.load()) {
            _step = _ota_ctx->result_step;
            _ota_ctx.reset();
        }
    }

    Step current_step = _step;
    UpdateInfo info   = _info;

    if (current_step != _last_rendered_step) {
        _last_rendered_step = current_step;
        switch (current_step) {
        case Step::CheckingGithub:
            _view->setInfo("Checking for updates...", "Connecting to GitHub...");
            _view->showSingleButton("Cancel");
            break;
        case Step::WifiNotConnected:
            _view->setInfo("Wi-Fi Not Connected", "Please connect to Wi-Fi\nin Settings first");
            _view->showSingleButton("Back");
            break;
        case Step::UpToDate:
            _view->setInfo("Your firmware is up to date!", fmt::format("Version: {}", common::FirmwareVersion));
            _view->showSingleButton("OK");
            break;
        case Step::CheckFailed:
            _view->setInfo("Check Failed", info.errorMsg);
            _view->showSingleButton("Back");
            break;
        case Step::UpdateAvailable:
            _view->setInfo("New Version Available!",
                           fmt::format("{} ({})", info.latestVersion, format_bytes(info.fileSize)));
            _view->showActionButtons("Later", "Update");
            break;
        case Step::Downloading:
            _view->setInfo("Downloading Update...", "Please do not turn off watch");
            _view->showProgressBar(true);
            _view->hideAllButtons();
            break;
        case Step::UpdateSuccess:
            _view->setInfo("Update Complete!", "Rebooting into new version...");
            _view->setProgress(100, info.totalBytes, info.totalBytes);
            _view->hideAllButtons();
            if (_reboot_at == 0) {
                _reboot_at = GetHAL().millis() + 2000;
            }
            break;
        case Step::UpdateFailed:
            _view->setInfo("Update Failed", info.errorMsg);
            _view->showProgressBar(false);
            _view->showSingleButton("Back");
            break;
        default:
            break;
        }
    }

    switch (current_step) {
    case Step::CheckingWifi: {
        auto wifiStatus = GetHAL().getWifiStatus();
        if (wifiStatus.connected && !wifiStatus.ipAddress.empty()) {
            _step = Step::CheckingGithub;
            _startCheckTask();
        } else {
            _step = Step::WifiNotConnected;
        }
        break;
    }

    case Step::CheckingGithub: {
        if (_view->consumeCenterClicked() || GetHAL().btnA.wasClicked() || GetHAL().btnB.wasClicked()) {
            _stopTask();
            _is_done = true;
            return;
        }
        break;
    }

    case Step::WifiNotConnected:
    case Step::CheckFailed:
    case Step::UpToDate: {
        if (_view->consumeCenterClicked() || GetHAL().btnA.wasClicked() || GetHAL().btnB.wasClicked()) {
            _is_done = true;
        }
        break;
    }

    case Step::UpdateAvailable: {
        if (_view->consumeLaterClicked() || _view->consumeCenterClicked() || GetHAL().btnA.wasClicked()) {
            _is_done = true;
        } else if (_view->consumeUpdateClicked() || GetHAL().btnB.wasClicked()) {
            _step = Step::Downloading;
            _startOtaTask();
        }
        break;
    }

    case Step::Downloading: {
        _view->setProgress(info.progress, info.bytesReceived, info.totalBytes);
        break;
    }

    case Step::UpdateSuccess: {
        if (_reboot_at > 0 && GetHAL().millis() >= _reboot_at) {
            mclog::tagInfo(_tag, "Rebooting system...");
            esp_restart();
        }
        break;
    }

    case Step::UpdateFailed: {
        if (_view->consumeCenterClicked() || GetHAL().btnA.wasClicked() || GetHAL().btnB.wasClicked()) {
            _is_done = true;
        }
        break;
    }

    default:
        break;
    }
}

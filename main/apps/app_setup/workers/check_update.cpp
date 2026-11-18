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
#include <esp_system.h>

#include <cctype>
#include <cstdio>
#include <cstring>
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
    size_t max_size = 64 * 1024;
};

esp_err_t http_event_cb(esp_http_client_event_t* evt)
{
    auto* b = static_cast<HttpRespBuf*>(evt->user_data);
    if (evt->event_id == HTTP_EVENT_ON_DATA && b != nullptr && evt->data != nullptr && evt->data_len > 0) {
        if (b->body.size() + evt->data_len <= b->max_size) {
            b->body.append(static_cast<const char*>(evt->data), evt->data_len);
        }
    }
    return ESP_OK;
}

}  // namespace

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

        _created_at = GetHAL().millis();
        _btn_center->onClick().connect([this]() {
            if (GetHAL().millis() - _created_at >= 400) {
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
        lv_obj_add_flag(_btn_left->get(), LV_OBJ_FLAG_HIDDEN);

        _btn_left->onClick().connect([this]() {
            if (GetHAL().millis() - _created_at >= 400) {
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
        lv_obj_add_flag(_btn_right->get(), LV_OBJ_FLAG_HIDDEN);

        _btn_right->onClick().connect([this]() {
            if (GetHAL().millis() - _created_at >= 400) {
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
    _abort_requested = true;
    auto client = static_cast<esp_http_client_handle_t>(_http_client.load());
    if (client) {
        esp_http_client_cancel_request(client);
    }
    while (_task_running) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    _task_handle = nullptr;
}

void CheckUpdateWorker::_startCheckTask()
{
    _abort_requested = false;
    _task_running   = true;

    xTaskCreate(
        [](void* arg) {
            auto* self = static_cast<CheckUpdateWorker*>(arg);

            HttpRespBuf resp;
            esp_http_client_config_t config = {};
            config.url                      = GITHUB_LATEST_URL;
            config.method                   = HTTP_METHOD_GET;
            config.crt_bundle_attach        = esp_crt_bundle_attach;
            config.timeout_ms               = 8000;
            config.buffer_size              = 4096;
            config.buffer_size_tx           = 1024;
            config.max_redirection_count    = 5;
            config.event_handler            = http_event_cb;
            config.user_data                = &resp;

            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (!client) {
                std::lock_guard<std::mutex> lk(self->_mutex);
                self->_step          = Step::CheckFailed;
                self->_info.errorMsg = "HTTP client init failed";
                self->_task_running  = false;
                vTaskDelete(NULL);
                return;
            }

            self->_http_client.store(client);
            esp_http_client_set_header(client, "User-Agent", "M5StopWatch");
            esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");

            esp_err_t err = esp_http_client_perform(client);
            int status    = esp_http_client_get_status_code(client);
            self->_http_client.store(nullptr);
            esp_http_client_cleanup(client);

            // If /releases/latest returned 404 (e.g. only prereleases exist), fallback to /releases
            if (err == ESP_OK && status == 404 && !self->_abort_requested) {
                resp.body.clear();
                config.url = GITHUB_RELEASES_URL;
                client     = esp_http_client_init(&config);
                if (client) {
                    self->_http_client.store(client);
                    esp_http_client_set_header(client, "User-Agent", "M5StopWatch");
                    esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");
                    err    = esp_http_client_perform(client);
                    status = esp_http_client_get_status_code(client);
                    self->_http_client.store(nullptr);
                    esp_http_client_cleanup(client);
                }
            }

            if (self->_abort_requested) {
                self->_task_running = false;
                vTaskDelete(NULL);
                return;
            }

            if (err != ESP_OK || status != 200 || resp.body.empty()) {
                std::lock_guard<std::mutex> lk(self->_mutex);
                self->_step = Step::CheckFailed;
                char err_buf[64];
                if (err != ESP_OK) {
                    snprintf(err_buf, sizeof(err_buf), "Network error (%s)", esp_err_to_name(err));
                } else {
                    snprintf(err_buf, sizeof(err_buf), "Server error (HTTP %d)", status);
                }
                self->_info.errorMsg = err_buf;
                self->_task_running  = false;
                vTaskDelete(NULL);
                return;
            }

            // Parse response JSON
            JsonDocument doc;
            DeserializationError d_err = deserializeJson(doc, resp.body);
            if (d_err != DeserializationError::Ok) {
                std::lock_guard<std::mutex> lk(self->_mutex);
                self->_step          = Step::CheckFailed;
                self->_info.errorMsg = "Failed to parse release info";
                self->_task_running  = false;
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
                    for (JsonObject asset : assets) {
                        const char* asset_name = asset["name"] | "";
                        if (strcmp(asset_name, "firmware-ota.bin") == 0 ||
                            (strlen(asset_name) > 4 && strcmp(asset_name + strlen(asset_name) - 4, ".bin") == 0)) {
                            highest_found = rel_ver;
                            best_version  = (tag[0] != '\0') ? tag : name;
                            best_url      = asset["browser_download_url"] | "";
                            best_size     = asset["size"] | 0;
                            break;
                        }
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

            std::lock_guard<std::mutex> lk(self->_mutex);
            if (highest_found > current_ver && !best_url.empty()) {
                self->_step               = Step::UpdateAvailable;
                self->_info.latestVersion = best_version;
                self->_info.downloadUrl   = best_url;
                self->_info.fileSize      = best_size;
            } else {
                self->_step = Step::UpToDate;
            }

            self->_task_running = false;
            vTaskDelete(NULL);
        },
        "check_update_task", 16 * 1024, this, 5, &_task_handle);
}

void CheckUpdateWorker::_startOtaTask()
{
    _abort_requested = false;
    _task_running   = true;

    xTaskCreate(
        [](void* arg) {
            auto* self = static_cast<CheckUpdateWorker*>(arg);

            std::string url;
            size_t expected_size = 0;
            {
                std::lock_guard<std::mutex> lk(self->_mutex);
                url           = self->_info.downloadUrl;
                expected_size = self->_info.fileSize;
            }

            mclog::tagInfo(_tag, "Starting HTTPS OTA from: {}", url);

            esp_http_client_config_t http_config = {};
            http_config.url                      = url.c_str();
            http_config.crt_bundle_attach        = esp_crt_bundle_attach;
            http_config.timeout_ms               = 15000;
            http_config.keep_alive_enable        = true;
            http_config.buffer_size              = 4096;
            http_config.buffer_size_tx           = 2048;
            http_config.max_redirection_count    = 5;
            http_config.user_agent               = "M5StopWatch";

            esp_https_ota_config_t ota_config = {};
            ota_config.http_config            = &http_config;

            esp_https_ota_handle_t ota_handle = nullptr;
            esp_err_t err                     = esp_https_ota_begin(&ota_config, &ota_handle);
            if (err != ESP_OK || ota_handle == nullptr) {
                mclog::tagError(_tag, "esp_https_ota_begin failed: {}", esp_err_to_name(err));
                std::lock_guard<std::mutex> lk(self->_mutex);
                self->_step          = Step::UpdateFailed;
                self->_info.errorMsg = "OTA begin failed (" + std::string(esp_err_to_name(err)) + ")";
                self->_task_running  = false;
                vTaskDelete(NULL);
                return;
            }

            while (!self->_abort_requested) {
                err = esp_https_ota_perform(ota_handle);
                if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                    break;
                }

                int read_len  = esp_https_ota_get_image_len_read(ota_handle);
                int total_len = esp_https_ota_get_image_size(ota_handle);
                if (total_len <= 0 && expected_size > 0) {
                    total_len = expected_size;
                }
                int progress = (total_len > 0) ? (read_len * 100 / total_len) : 0;
                if (progress > 100) progress = 100;

                {
                    std::lock_guard<std::mutex> lk(self->_mutex);
                    self->_info.bytesReceived = read_len;
                    self->_info.totalBytes    = total_len;
                    self->_info.progress      = progress;
                }
            }

            if (self->_abort_requested) {
                mclog::tagWarn(_tag, "OTA aborted by user");
                esp_https_ota_abort(ota_handle);
                self->_task_running = false;
                vTaskDelete(NULL);
                return;
            }

            if (err == ESP_OK) {
                if (esp_https_ota_is_complete_data_received(ota_handle)) {
                    esp_err_t finish_err = esp_https_ota_finish(ota_handle);
                    if (finish_err == ESP_OK) {
                        mclog::tagInfo(_tag, "OTA update successfully completed");
                        std::lock_guard<std::mutex> lk(self->_mutex);
                        self->_step          = Step::UpdateSuccess;
                        self->_info.progress = 100;
                        self->_task_running  = false;
                        vTaskDelete(NULL);
                        return;
                    } else {
                        mclog::tagError(_tag, "esp_https_ota_finish failed: {}", esp_err_to_name(finish_err));
                        err = finish_err;
                    }
                } else {
                    mclog::tagError(_tag, "Incomplete data received in OTA");
                    err = ESP_ERR_INVALID_SIZE;
                }
            }

            esp_https_ota_abort(ota_handle);
            {
                std::lock_guard<std::mutex> lk(self->_mutex);
                self->_step          = Step::UpdateFailed;
                self->_info.errorMsg = "OTA failed (" + std::string(esp_err_to_name(err)) + ")";
            }

            self->_task_running = false;
            vTaskDelete(NULL);
        },
        "ota_download_task", 16 * 1024, this, 5, &_task_handle);
}

void CheckUpdateWorker::update()
{
    if (!_view) return;

    Step current_step;
    UpdateInfo info;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        current_step = _step;
        info         = _info;
    }

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
            {
                std::lock_guard<std::mutex> lk(_mutex);
                _step = Step::CheckingGithub;
            }
            _startCheckTask();
        } else {
            std::lock_guard<std::mutex> lk(_mutex);
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
            {
                std::lock_guard<std::mutex> lk(_mutex);
                _step = Step::Downloading;
            }
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

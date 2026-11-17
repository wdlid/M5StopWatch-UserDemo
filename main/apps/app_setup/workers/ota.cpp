/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <apps/common/key_manager/key_manager.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <uitk/short_namespace.hpp>

#include <cstdio>
#include <string>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace setup_workers;

namespace {
constexpr const char* _tag = "Setup-OTA";

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

}  // namespace

class OtaWorker::OtaView {
public:
    OtaView()
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
        _title_label->setText("OTA Update");
        _title_label->setTextFont(&MontserratSemiBold26);
        _title_label->setTextColor(lv_color_hex(0xFFFFFF));
        _title_label->align(LV_ALIGN_TOP_MID, 0, 42);

        _info_label = std::make_unique<Label>(_panel->get());
        _info_label->setTextFont(&lv_font_montserrat_22);
        _info_label->setTextColor(lv_color_hex(0x94A3B8));
        _info_label->setWidth(380);
        _info_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _info_label->setLongMode(LV_LABEL_LONG_MODE_WRAP);
        _info_label->align(LV_ALIGN_CENTER, 0, -45);
        _info_label->setText("Starting OTA service...");

        _status_label = std::make_unique<Label>(_panel->get());
        _status_label->setTextFont(&lv_font_montserrat_24);
        _status_label->setTextColor(lv_color_hex(0x4AD78C));
        _status_label->setWidth(380);
        _status_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _status_label->setLongMode(LV_LABEL_LONG_MODE_WRAP);
        _status_label->align(LV_ALIGN_CENTER, 0, 35);
        _status_label->setText("Initializing...");

        // Progress bar container
        _bar_obj = lv_bar_create(_panel->get());
        lv_obj_align(_bar_obj, LV_ALIGN_CENTER, 0, 85);
        lv_obj_set_size(_bar_obj, 340, 20);
        lv_bar_set_range(_bar_obj, 0, 100);
        lv_bar_set_value(_bar_obj, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(_bar_obj, lv_color_hex(0x1E293B), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_bar_obj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(_bar_obj, 10, LV_PART_MAIN);
        lv_obj_set_style_bg_color(_bar_obj, lv_color_hex(0x4AD78C), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(_bar_obj, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(_bar_obj, 10, LV_PART_INDICATOR);

        // Exit / Back button
        _btn = std::make_unique<Button>(_panel->get());
        _btn->align(LV_ALIGN_CENTER, 0, 165);
        _btn->setSize(280, 64);
        _btn->setRadius(32);
        _btn->setBorderWidth(0);
        _btn->setShadowWidth(0);
        _btn->setBgColor(lv_color_hex(0x334155));
        _btn->label().setText("Exit");
        _btn->label().setTextFont(&lv_font_montserrat_24);
        _btn->label().setTextColor(lv_color_hex(0xF8FAFC));
        _btn->label().align(LV_ALIGN_CENTER, 0, 0);
        _created_at = GetHAL().millis();
        _btn->onClick().connect([this]() {
            if (GetHAL().millis() - _created_at >= 800) {
                _exit_requested = true;
            }
        });
    }

    void updateStatus(const Hal::OtaStatus& status)
    {
        // Update connection instructions
        if (!status.ipAddress.empty()) {
            std::string info;
            if (!status.ssid.empty() && status.ssid.find("OTA") == std::string::npos) {
                info = "Wi-Fi: " + status.ssid + "\nOpen http://" + status.ipAddress + "/ota";
            } else {
                info = "Connect Wi-Fi:\n" + status.ssid + "\nhttp://" + status.ipAddress + "/ota";
            }
            _info_label->setText(info);
        }

        // Update progress bar
        lv_bar_set_value(_bar_obj, status.progress, LV_ANIM_OFF);

        // Update state display
        switch (status.state) {
        case Hal::OtaState::Idle:
        case Hal::OtaState::Ready:
            _status_label->setTextColor(lv_color_hex(0x4AD78C));
            _status_label->setText("Ready for firmware");
            setButtonVisible(true);
            break;
        case Hal::OtaState::Flashing:
            _status_label->setTextColor(lv_color_hex(0x38BDF8));
            _status_label->setText("Flashing " + std::to_string(status.progress) + "% (" +
                                  format_bytes(status.bytesReceived) + ")");
            // Hide exit button to avoid disrupting flash write
            setButtonVisible(false);
            break;
        case Hal::OtaState::Success:
            _status_label->setTextColor(lv_color_hex(0x10B981));
            _status_label->setText("Complete! Rebooting...");
            setButtonVisible(false);
            break;
        case Hal::OtaState::Failed:
            _status_label->setTextColor(lv_color_hex(0xF43F5E));
            _status_label->setText("Failed: " + status.message);
            setButtonVisible(true);
            _btn->label().setText("Back");
            break;
        }
    }

    bool consumeExitRequested()
    {
        if (GetHAL().millis() - _created_at < 800) {
            _exit_requested = false;
            return false;
        }
        bool requested = _exit_requested;
        _exit_requested = false;
        return requested;
    }

    void setButtonVisible(bool visible)
    {
        if (_btn) {
            if (visible) {
                lv_obj_remove_flag(_btn->get(), LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(_btn->get(), LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

private:
    std::unique_ptr<Container> _panel;
    std::unique_ptr<Label> _title_label;
    std::unique_ptr<Label> _info_label;
    std::unique_ptr<Label> _status_label;
    lv_obj_t* _bar_obj = nullptr;
    std::unique_ptr<Button> _btn;
    uint32_t _created_at = 0;
    bool _exit_requested = false;
};

OtaWorker::OtaWorker()
{
    mclog::tagInfo(_tag, "start ota worker");
    _view = std::make_unique<OtaView>();

    // Start OTA service
    GetHAL().startOta([](std::string_view msg) {
        mclog::tagInfo(_tag, "{}", msg);
    });
}

OtaWorker::~OtaWorker()
{
    mclog::tagInfo(_tag, "exit ota worker");
    auto status = GetHAL().getOtaStatus();
    if (status.state != Hal::OtaState::Success) {
        GetHAL().stopOta();
    }
}

void OtaWorker::update()
{
    if (!_view) return;

    auto status = GetHAL().getOtaStatus();
    _view->updateStatus(status);

    // Can only exit if not currently flashing
    if (status.state != Hal::OtaState::Flashing) {
        if (_view->consumeExitRequested()) {
            _is_done = true;
        }
    }
}

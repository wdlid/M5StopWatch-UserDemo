/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_setup.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>

using namespace mooncake;
using namespace view;
using namespace setup_workers;

namespace {

std::string format_timeout(uint32_t seconds)
{
    if (seconds == 0) {
        return "Never";
    }
    if (seconds < 60) {
        return fmt::format("{}s", seconds);
    }
    return fmt::format("{}m", seconds / 60);
}

const char* format_color_temp(Hal::ColorTemp temp)
{
    switch (temp) {
    case Hal::ColorTemp::Cool:
        return "Cool";
    case Hal::ColorTemp::SoftWarm:
        return "Soft Warm";
    case Hal::ColorTemp::Warm:
        return "Warm";
    default:
        return "Cool";
    }
}

const char* format_button_cfg(const Hal::ButtonConfig& cfg)
{
    if (cfg.sfxEnabled && cfg.vibrateEnabled) {
        return "SFX + Vib";
    }
    if (cfg.sfxEnabled) {
        return "SFX Only";
    }
    if (cfg.vibrateEnabled) {
        return "Vib Only";
    }
    return "Silent";
}

std::string format_wifi_status(const Hal::WifiStatus& status)
{
    switch (status.mode) {
    case Hal::WifiMode::Off:
        return "Off";
    case Hal::WifiMode::Connecting:
        return status.ssid.empty() ? "Connecting" : status.ssid;
    case Hal::WifiMode::Connected:
        return status.ssid.empty() ? "Connected" : status.ssid;
    case Hal::WifiMode::ConfigAp:
        return "Setup AP";
    case Hal::WifiMode::BadgeAp:
        return "Badge AP";
    case Hal::WifiMode::Error:
        return "Error";
    }
    return "Off";
}

const char* format_timezone_short(const std::string& tz)
{
    if (tz == "CST-8") {
        return "UTC+8";
    }
    if (tz == "JST-9") {
        return "UTC+9";
    }
    if (tz == "GMT0") {
        return "UTC+0";
    }
    if (tz.find("CET") != std::string::npos) {
        return "UTC+1";
    }
    if (tz.find("EST") != std::string::npos) {
        return "UTC-5";
    }
    if (tz.find("PST") != std::string::npos) {
        return "UTC-8";
    }
    return "Custom";
}

}  // namespace

AppSetup::AppSetup()
{
    setAppInfo().name = "Settings";
    setAppInfo().icon = (void*)&icon_setup;
}

void AppSetup::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppSetup::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager = std::make_unique<input::KeyManager>();

    _current_page     = MenuPageId::Root;
    _pending_page     = MenuPageId::Root;
    _root_scroll_index = 0;
    _sub_scroll_index  = 0;
    _switch_menu      = false;
    _destroy_menu     = false;
    _need_warm_reset  = false;

    LvglLockGuard lock;
    open_menu(MenuPageId::Root);
}

void AppSetup::open_menu(MenuPageId page)
{
    _current_page = page;
    _menu_page    = std::make_unique<view::SelectMenuPage>(build_menu_config(page));
}

view::SelectMenuPage::Config AppSetup::build_menu_config(MenuPageId page)
{
    SelectMenuPage::Config cfg;

    auto launch_worker = [this](auto factory) {
        if (_menu_page) {
            if (_current_page == MenuPageId::Root) {
                _root_scroll_index = _menu_page->currentCenterIndex();
            } else {
                _sub_scroll_index = _menu_page->currentCenterIndex();
            }
        }
        _destroy_menu = true;
        _worker       = factory();
    };

    auto goto_page = [this](MenuPageId target) {
        if (_menu_page && _current_page == MenuPageId::Root && target != MenuPageId::Root) {
            _root_scroll_index = _menu_page->currentCenterIndex();
            _sub_scroll_index  = 0;
        }
        _pending_page = target;
        _switch_menu  = true;
    };

    switch (page) {
    case MenuPageId::Root: {
        cfg.title              = "Settings";
        cfg.leftActionText     = "Exit";
        cfg.leftActionColor    = 0x9EB3C2;
        cfg.initialScrollIndex = _root_scroll_index;
        cfg.onLeftAction       = [this]() { close(); };

        const int brightness      = GetHAL().getBackLightBrightness();
        const uint32_t timeout    = GetHAL().getScreenTimeout();
        const int volume          = GetHAL().getSpeakerVolume();
        const auto btn_cfg        = GetHAL().getButtonConfig();
        const auto wifi_status    = GetHAL().getWifiStatus();
        const auto time_hms       = GetHAL().getTimeHms();
        const std::string tz      = GetHAL().getTimezone();

        cfg.items = {
            {
                "Display",
                fmt::format("{}%  /  {}", brightness, format_timeout(timeout)),
                LV_SYMBOL_IMAGE,
                0xFFB74D,
                true,
                [goto_page]() { goto_page(MenuPageId::Display); },
            },
            {
                "Sound & Touch",
                fmt::format("{}%  /  {}", volume, format_button_cfg(btn_cfg)),
                LV_SYMBOL_VOLUME_MAX,
                0x4FC3F7,
                true,
                [goto_page]() { goto_page(MenuPageId::Sound); },
            },
            {
                "Wi-Fi",
                format_wifi_status(wifi_status),
                LV_SYMBOL_WIFI,
                0x4AD78C,
                true,
                [launch_worker]() { launch_worker([]() { return std::make_unique<WifiWorker>(); }); },
            },
            {
                "Time & Date",
                fmt::format("{:02d}:{:02d}  /  {}", time_hms.hour, time_hms.minute, format_timezone_short(tz)),
                LV_SYMBOL_REFRESH,
                0xBA68C8,
                true,
                [goto_page]() { goto_page(MenuPageId::DateTime); },
            },
            {
                "System",
                fmt::format("v{}", common::FirmwareVersion),
                LV_SYMBOL_SETTINGS,
                0x90A4AE,
                true,
                [goto_page]() { goto_page(MenuPageId::System); },
            },
        };
        break;
    }

    case MenuPageId::Display: {
        cfg.title              = "Display";
        cfg.leftActionText     = "Back";
        cfg.leftActionColor    = 0x9EB3C2;
        cfg.initialScrollIndex = _sub_scroll_index;
        cfg.onLeftAction       = [goto_page]() { goto_page(MenuPageId::Root); };

        cfg.items = {
            {
                "Brightness",
                fmt::format("{}%", GetHAL().getBackLightBrightness()),
                LV_SYMBOL_EYE_OPEN,
                0xFFB74D,
                true,
                [launch_worker]() { launch_worker([]() { return std::make_unique<BrightnessWorker>(); }); },
            },
            {
                "Screen Timeout",
                format_timeout(GetHAL().getScreenTimeout()),
                LV_SYMBOL_BELL,
                0xFF8A65,
                true,
                [launch_worker]() { launch_worker([]() { return std::make_unique<ScreenTimeoutWorker>(); }); },
            },
            {
                "Color Temp",
                format_color_temp(GetHAL().getColorTemperature()),
                LV_SYMBOL_TINT,
                0xFFD54F,
                true,
                [launch_worker]() { launch_worker([]() { return std::make_unique<ColorTempWorker>(); }); },
            },
            {
                "Eye Care",
                GetHAL().getEyeCareReminder() ? "10m Reminder On" : "Off",
                LV_SYMBOL_LOOP,
                0x81C784,
                true,
                [launch_worker]() { launch_worker([]() { return std::make_unique<EyeCareWorker>(); }); },
            },
        };
        break;
    }

    case MenuPageId::Sound: {
        cfg.title              = "Sound";
        cfg.leftActionText     = "Back";
        cfg.leftActionColor    = 0x9EB3C2;
        cfg.initialScrollIndex = _sub_scroll_index;
        cfg.onLeftAction       = [goto_page]() { goto_page(MenuPageId::Root); };

        cfg.items = {
            {
                "Volume",
                fmt::format("{}%", GetHAL().getSpeakerVolume()),
                LV_SYMBOL_VOLUME_MAX,
                0x4FC3F7,
                true,
                [launch_worker]() { launch_worker([]() { return std::make_unique<VolumeWorker>(); }); },
            },
            {
                "Button Feedback",
                format_button_cfg(GetHAL().getButtonConfig()),
                LV_SYMBOL_OK,
                0x4DD0E1,
                true,
                [launch_worker]() { launch_worker([]() { return std::make_unique<ButtonWorker>(); }); },
            },
        };
        break;
    }

    case MenuPageId::DateTime: {
        cfg.title              = "Time & Date";
        cfg.leftActionText     = "Back";
        cfg.leftActionColor    = 0x9EB3C2;
        cfg.initialScrollIndex = _sub_scroll_index;
        cfg.onLeftAction       = [goto_page]() { goto_page(MenuPageId::Root); };

        const auto t = GetHAL().getTimeHms();
        const auto d = GetHAL().getDateYmd();

        cfg.items = {
            {
                "Set Time",
                fmt::format("{:02d}:{:02d}:{:02d}", t.hour, t.minute, t.second),
                LV_SYMBOL_REFRESH,
                0xBA68C8,
                true,
                [launch_worker]() { launch_worker([]() { return std::make_unique<SetTimeWorker>(); }); },
            },
            {
                "Set Date",
                fmt::format("{:04d}-{:02d}-{:02d}", d.year, d.month, d.day),
                LV_SYMBOL_LIST,
                0x9575CD,
                true,
                [launch_worker]() { launch_worker([]() { return std::make_unique<SetDateWorker>(); }); },
            },
            {
                "Time Zone",
                format_timezone_short(GetHAL().getTimezone()),
                LV_SYMBOL_GPS,
                0x7986CB,
                true,
                [launch_worker]() { launch_worker([]() { return std::make_unique<TimezoneWorker>(); }); },
            },
        };
        break;
    }

    case MenuPageId::System: {
        cfg.title              = "System";
        cfg.leftActionText     = "Back";
        cfg.leftActionColor    = 0x9EB3C2;
        cfg.initialScrollIndex = _sub_scroll_index;
        cfg.onLeftAction       = [goto_page]() { goto_page(MenuPageId::Root); };

        cfg.items = {
            {
                "OTA Update",
                "Local Web Upload",
                LV_SYMBOL_UPLOAD,
                0x90A4AE,
                true,
                [launch_worker]() { launch_worker([]() { return std::make_unique<OtaWorker>(); }); },
            },
            {
                "Firmware",
                fmt::format("v{}  /  Check Update", common::FirmwareVersion),
                LV_SYMBOL_DOWNLOAD,
                0x4AD78C,
                true,
                [launch_worker]() { launch_worker([]() { return std::make_unique<CheckUpdateWorker>(); }); },
            },
        };
        break;
    }
    }

    return cfg;
}

void AppSetup::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    if (GetHAL().isScreenSleeping()) {
        return;
    }

    LvglLockGuard lock;

    if (_menu_page) {
        _menu_page->update();
    }

    if (_switch_menu) {
        _switch_menu = false;
        _menu_page.reset();
        open_menu(_pending_page);
        return;
    }

    if (_destroy_menu) {
        _menu_page.reset();
        _destroy_menu = false;
        return;
    }

    if (_worker) {
        _worker->update();
        if (_worker->isDone()) {
            _worker.reset();
            open_menu(_current_page);
        }
    }
}

void AppSetup::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    _key_manager.reset();

    LvglLockGuard lock;

    _menu_page.reset();
    _worker.reset();

    if (_need_warm_reset) {
        // GetHAL().requestWarmReboot(6);
    }
}

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "clock_faces.h"
#include <apps/common/key_manager/key_manager.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <smooth_lvgl.hpp>
#include <functional>
#include <vector>
#include <memory>

namespace view {

/**
 * @brief Horizontal 2-page launcher:
 *   Page 0 (Left, default): Echo-inspired circular clock faces (vertical swipe to switch faces)
 *   Page 1 (Right):         Orbital app ring desktop with symmetrical 3-zone status header
 */
class LauncherView {
public:
    ~LauncherView();

    std::function<void(int appID)> onAppClicked;
    std::function<void()> onStatusBarClicked;

    void init(std::vector<mooncake::AppProps_t> appProps);
    void pollInput();
    bool hasPendingImmediateWork() const;
    void update();

    bool isOnClockPage() const;
    void scrollToClock(bool anim = true);

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pager;
    std::unique_ptr<ClockFacesView> _clock_faces;
    std::unique_ptr<uitk::lvgl_cpp::Container> _desktop_page;
    std::unique_ptr<uitk::lvgl_cpp::Container> _status_bar;
    std::unique_ptr<uitk::lvgl_cpp::Container> _drag_handle;
    std::unique_ptr<uitk::lvgl_cpp::Container> _wifi_pill;
    std::unique_ptr<uitk::lvgl_cpp::Container> _bat_pill;
    std::unique_ptr<uitk::lvgl_cpp::Container> _bat_icon_wrap;
    std::unique_ptr<uitk::lvgl_cpp::Label> _wifi_icon;
    std::unique_ptr<uitk::lvgl_cpp::Label> _wifi_label;
    std::unique_ptr<uitk::lvgl_cpp::Bar> _bat_bar;
    std::unique_ptr<uitk::lvgl_cpp::Container> _bat_tip;
    std::unique_ptr<uitk::lvgl_cpp::Image> _bat_lightning;
    std::unique_ptr<uitk::lvgl_cpp::Label> _bat_label;

    std::unique_ptr<uitk::lvgl_cpp::Container> _orbit_panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _orbit_hub;
    std::unique_ptr<uitk::lvgl_cpp::Label> _orbit_time;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Container>> _app_cards;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Container>> _icon_badges;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Image>> _icon_images;
    std::vector<size_t> _badge_swatch_indices;
    std::unique_ptr<input::KeyManager> _key_manager;

    input::KeyEvent _pending_key_event = input::KeyEvent::None;
    int _clicked_app_id                = -1;
    bool _pager_scrolling              = false;
    uint32_t _last_status_tick         = 0;
    uint32_t _last_anim_tick           = 0;
    uint8_t _conn_anim_step            = 0;
    int _last_orbit_hour               = -1;
    int _last_orbit_min                = -1;
    int _last_bat_level                = -1;
    bool _last_bat_charging            = false;
    int _last_wifi_state_key           = -1;
    Hal::ThemeMode _last_theme         = Hal::ThemeMode::Dark;
    uint16_t* _light_grad_lut          = nullptr;

    void ensure_light_grad_lut();
    void update_status_info(bool force = false);
    void apply_theme();
    static void desktop_cover_check_cb(lv_event_t* e);
    static void desktop_draw_main_cb(lv_event_t* e);
};

/**
 * @brief
 *
 */
class GuidePage {
public:
    GuidePage();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Image> _img;
};

}  // namespace view

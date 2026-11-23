/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <smooth_lvgl.hpp>
#include <functional>
#include <vector>
#include <memory>

namespace view {

/**
 * @brief Single-page 2-column app grid launcher with direct top status bar.
 */
class LauncherView {
public:
    ~LauncherView();

    std::function<void(int appID)> onAppClicked;
    std::function<void()> onStatusBarClicked;

    void init(std::vector<mooncake::AppProps_t> appProps);
    void update();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _status_bar;
    std::unique_ptr<uitk::lvgl_cpp::Container> _drag_handle;
    std::unique_ptr<uitk::lvgl_cpp::Label> _wifi_icon;
    std::unique_ptr<uitk::lvgl_cpp::Label> _wifi_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _time_label;
    std::unique_ptr<uitk::lvgl_cpp::Bar> _bat_bar;
    std::unique_ptr<uitk::lvgl_cpp::Container> _bat_tip;
    std::unique_ptr<uitk::lvgl_cpp::Image> _bat_lightning;
    std::unique_ptr<uitk::lvgl_cpp::Label> _bat_label;

    std::unique_ptr<uitk::lvgl_cpp::Container> _grid_panel;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Container>> _app_cards;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Container>> _icon_badges;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Image>> _icon_images;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Label>> _app_labels;
    std::unique_ptr<input::KeyManager> _key_manager;

    int _clicked_app_id       = -1;
    uint32_t _last_status_tick = 0;

    void update_status_info();
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

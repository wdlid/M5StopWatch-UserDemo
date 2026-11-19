/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "arc_action_bar.h"
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace view {

class SelectMenuPage {
public:
    struct MenuItem {
        std::string title;
        std::string subtitle;
        const char* icon     = nullptr;
        uint32_t accentColor = 0x4AD78C;
        bool showChevron     = true;
        std::function<void()> onClick;
    };

    struct Config {
        std::string title;
        std::string leftActionText = "Exit";
        uint32_t leftActionColor   = 0x9EB3C2;
        std::function<void()> onLeftAction;
        std::vector<MenuItem> items;
        int initialScrollIndex = 0;
    };

    explicit SelectMenuPage(Config config);
    ~SelectMenuPage();

    void update();
    int currentCenterIndex() const;

private:
    struct RowWidgets {
        std::unique_ptr<uitk::lvgl_cpp::Container> card;
        std::unique_ptr<uitk::lvgl_cpp::Container> badge;
        std::unique_ptr<uitk::lvgl_cpp::Label> iconLabel;
        std::unique_ptr<uitk::lvgl_cpp::Label> titleLabel;
        std::unique_ptr<uitk::lvgl_cpp::Label> subtitleLabel;
        std::unique_ptr<uitk::lvgl_cpp::Label> chevronLabel;
        int centerY      = 0;
        int32_t lastWidth = -1;
        lv_opa_t lastOpa  = 0;
    };

    Config _config;
    std::unique_ptr<uitk::lvgl_cpp::Container> _root;
    std::unique_ptr<uitk::lvgl_cpp::Container> _list;
    std::unique_ptr<uitk::lvgl_cpp::Container> _top_fade;
    std::unique_ptr<ArcActionBar> _action_bar;
    std::vector<RowWidgets> _rows;

    int _pending_item_index    = -1;
    bool _pending_left_action  = false;
    bool _is_scrolling         = false;
    bool _suppress_next_click  = false;
    uint32_t _scroll_end_ms    = 0;
    lv_point_t _press_point    = {0, 0};

    void create_item_row(int y, const MenuItem& item, int itemIdx);
    void update_radial_effects();
};

}  // namespace view

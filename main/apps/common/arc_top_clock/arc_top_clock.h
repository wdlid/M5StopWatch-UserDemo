/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
// Refs: https://developer.android.com/design/ui/wear/guides/foundations/getting-started
#pragma once
#include <lvgl.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <smooth_lvgl.hpp>
#include <hal/hal.h>
#include <string>
#include <string_view>
#include <cstdint>
#include <ctime>
#include <memory>
#include <vector>

namespace view {

class ArcTopClock : public uitk::lvgl_cpp::Widget<lv_obj_create> {
public:
    using Widget::Widget;
    ~ArcTopClock();

    bool showFullDate       = false;
    uint32_t color          = 0xFFFFFF;
    uint32_t updateInterval = 1000;
    int displayRadius       = 233;

    void init();
    void update(bool force = false);

protected:
    struct ArcSegment {
        lv_obj_t* obj        = nullptr;
        std::string lastText = "";
        uint32_t lastColor   = 0;
    };

    std::vector<std::unique_ptr<uitk::lvgl_cpp::Label>> labels;

    // Compact HH:MM mode uses labels[]; full-date mode uses colored arc segments.
    ArcSegment _seg_date;
    ArcSegment _seg_time;
    ArcSegment _seg_weekday;
    ArcSegment _seg_temp;
    ArcSegment _seg_condition;
    ArcSegment _seg_aqi;

    uint32_t update_time_count = 0;
    Hal::ThemeMode _last_theme = Hal::ThemeMode::Dark;

    void init_full_date_arcs();
    void destroy_segment(ArcSegment& seg);
    lv_obj_t* create_arc_segment();
    void set_segment(ArcSegment& seg, std::string_view text, uint32_t color, float angle_start, float angle_size);
    void set_clock_to(const std::string_view text);
    void update_full_date(const std::tm* localTime);
};

}  // namespace view

/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "arc_top_clock.h"
#include <assets/assets.h>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <string>
#include <ctime>
#include <cmath>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace view;

static const std::vector<int> _label_rotation_map = {-100, -46, 0, 32, 84};
static const std::vector<int> _label_x_map        = {3, 3, 1, 4, 4};
static const std::vector<int> _label_y_map        = {0, 0, -2, -1, -1};

ArcTopClock::~ArcTopClock()
{
    if (_arc_label && lv_obj_is_valid(_arc_label)) {
        lv_obj_delete(_arc_label);
    }
    _arc_label = nullptr;
}

void ArcTopClock::init()
{
    if (showFullDate) {
        setSize(466, 466);
        setBorderWidth(0);
        setOutlineWidth(0);
        setPaddingAll(0);
        setBgOpa(LV_OPA_TRANSP);
        removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        removeFlag(LV_OBJ_FLAG_CLICKABLE);
        addFlag(LV_OBJ_FLAG_EVENT_BUBBLE);
        addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

        _arc_label = lv_arclabel_create(get());
        lv_obj_set_size(_arc_label, 466, 466);
        lv_obj_center(_arc_label);
        lv_arclabel_set_radius(_arc_label, 214);
        // Center the 20-char string around 256.5 deg so index 13 (':') lands at 270 deg (top center)
        lv_arclabel_set_angle_start(_arc_label, 196.5f);
        lv_arclabel_set_angle_size(_arc_label, 120.0f);
        lv_arclabel_set_dir(_arc_label, LV_ARCLABEL_DIR_CLOCKWISE);
        lv_arclabel_set_text_horizontal_align(_arc_label, LV_ARCLABEL_TEXT_ALIGN_CENTER);
        lv_arclabel_set_text_vertical_align(_arc_label, LV_ARCLABEL_TEXT_ALIGN_CENTER);
        lv_arclabel_set_overflow(_arc_label, LV_ARCLABEL_OVERFLOW_VISIBLE);
        lv_obj_set_style_text_font(_arc_label, &lv_font_maple_mono_medium_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(_arc_label, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_remove_flag(_arc_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(_arc_label, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(
            _arc_label,
            [](lv_event_t* e) {
                auto** ptr = static_cast<lv_obj_t**>(lv_event_get_user_data(e));
                if (ptr) {
                    *ptr = nullptr;
                }
            },
            LV_EVENT_DELETE, &_arc_label);
    } else {
        setSize(114, 36);
        setBorderWidth(0);
        setOutlineWidth(0);
        setPaddingAll(0);
        setBgOpa(LV_OPA_TRANSP);
        removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        labels.resize(5);
        for (size_t i = 0; i < labels.size(); i++) {
            labels[i] = std::make_unique<Label>(get());
            labels[i]->setText(".");
            labels[i]->setTextFont(&lv_font_maple_mono_medium_28);
            labels[i]->setTextColor(lv_color_hex(color));
            labels[i]->setTransformPivot(0, displayRadius);
            labels[i]->align(LV_ALIGN_CENTER, _label_x_map[i], _label_y_map[i]);
            labels[i]->setRotation(_label_rotation_map[i]);
        }
    }

    update(true);
}

void ArcTopClock::update(bool force)
{
    if (force || GetHAL().millis() - update_time_count > updateInterval) {
        std::time_t now    = std::time(nullptr);
        std::tm* localTime = std::localtime(&now);
        if (showFullDate) {
            static const char* const kWeekDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
            int wday = (localTime->tm_wday >= 0 && localTime->tm_wday < 7) ? localTime->tm_wday : 0;
            set_clock_to(fmt::format("{:04d}/{:02d}/{:02d} {:02d}:{:02d} {}",
                                     localTime->tm_year + 1900,
                                     localTime->tm_mon + 1,
                                     localTime->tm_mday,
                                     localTime->tm_hour,
                                     localTime->tm_min,
                                     kWeekDays[wday]));
        } else {
            set_clock_to(fmt::format("{:02d}:{:02d}", localTime->tm_hour, localTime->tm_min));
        }
        update_time_count = GetHAL().millis();
    }
}

void ArcTopClock::set_clock_to(const std::string_view text)
{
    if (_arc_label) {
        std::string transformed(text);
        for (char& ch : transformed) {
            if (ch == '0') {
                ch = 'O';
            }
        }
        if (transformed != _last_arc_text) {
            _last_arc_text = std::move(transformed);
            lv_arclabel_set_text(_arc_label, _last_arc_text.c_str());
        }
        return;
    }

    const int count = std::min((int)text.size(), (int)labels.size());
    for (int i = 0; i < count; i++) {
        const char target_char = (text[i] == '0') ? 'O' : text[i];
        const char* cur        = lv_label_get_text(labels[i]->get());
        if (cur && cur[0] == target_char && cur[1] == '\0') {
            continue;
        }
        char buf[2] = {target_char, '\0'};
        labels[i]->setText(buf);
    }
}

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

        const int total_chars = 20;
        const int center_idx  = 13;  // ':' in "YYYY/MM/DD HH:MM Day"
        const float r         = 214.0f;
        const float step_deg  = 3.85f;

        labels.resize(total_chars);
        for (int i = 0; i < total_chars; i++) {
            labels[i] = std::make_unique<Label>(get());
            labels[i]->setText(" ");
            labels[i]->setTextFont(&lv_font_maple_mono_medium_24);
            labels[i]->setTextColor(lv_color_hex(color));
            labels[i]->setPaddingAll(0);
            labels[i]->removeFlag(LV_OBJ_FLAG_CLICKABLE);
            labels[i]->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

            float deg = (i - center_idx) * step_deg;
            float rad = deg * static_cast<float>(M_PI / 180.0);
            int32_t offset_x = static_cast<int32_t>(std::round(r * std::sin(rad)));
            int32_t offset_y = static_cast<int32_t>(std::round(-r * std::cos(rad)));
            int16_t rotation = static_cast<int16_t>(std::round(deg * 10.0f));

            labels[i]->setTransformPivot(LV_PCT(50), LV_PCT(50));
            labels[i]->align(LV_ALIGN_CENTER, offset_x, offset_y);
            labels[i]->setRotation(rotation);
        }
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

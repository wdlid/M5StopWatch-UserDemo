/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "arc_top_clock.h"
#include <apps/common/weather/weather.h>
#include <assets/assets.h>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <string>
#include <ctime>
#include <algorithm>

using namespace uitk;
using namespace uitk::lvgl_cpp;
using namespace view;

static const std::vector<int> _label_rotation_map = {-100, -46, 0, 32, 84};
static const std::vector<int> _label_x_map        = {3, 3, 1, 4, 4};
static const std::vector<int> _label_y_map        = {0, 0, -2, -1, -1};

// Full-date layout: LEFT-aligned segments, colon of HH:MM near top (270°).
// "YYYY/MM/DD HH:MM Www" — colon at index 13 → start = 270 - 13*6 = 192
constexpr float kDegPerChar      = 6.0f;
constexpr float kBlockStart      = 192.0f;
constexpr float kDateStart       = kBlockStart;                  // idx 0
constexpr float kTimeStart       = kBlockStart + 11 * kDegPerChar;  // idx 11 "HH:MM"
constexpr float kWeekdayStart    = kBlockStart + 17 * kDegPerChar;  // idx 17 "Www"
constexpr float kWeatherStart    = kBlockStart + 21 * kDegPerChar;  // after weekday + gap

struct FullDatePalette {
    uint32_t date;
    uint32_t time;
    uint32_t weekday;
    uint32_t temp;
    uint32_t condition;
};

static FullDatePalette palette_for_theme(Hal::ThemeMode mode)
{
    if (mode == Hal::ThemeMode::Light) {
        // Deeper hues for contrast on celadon light background
        return FullDatePalette{
            .date      = 0x4A5F56,  // slate green
            .time      = 0x0F766E,  // deep teal (not pale)
            .weekday   = 0x6B7F75,  // muted
            .temp      = 0xC2410C,  // burnt orange
            .condition = 0x0369A1,  // strong sky blue
        };
    }
    // Dark AMOLED: vivid but not flat white
    return FullDatePalette{
        .date      = 0x94A3B8,  // cool slate
        .time      = 0x2DD4BF,  // teal accent
        .weekday   = 0x64748B,  // muted blue-gray
        .temp      = 0xFB923C,  // warm orange
        .condition = 0x38BDF8,  // sky
    };
}

static uint32_t aqi_color(weather::AqiLevel level, Hal::ThemeMode mode)
{
    const bool light = (mode == Hal::ThemeMode::Light);
    switch (level) {
        case weather::AqiLevel::Good:
            return light ? 0x15803D : 0x4ADE80;
        case weather::AqiLevel::Moderate:
            return light ? 0xA16207 : 0xFACC15;
        case weather::AqiLevel::UnhealthySg:
            return light ? 0xC2410C : 0xFB923C;
        case weather::AqiLevel::Unhealthy:
            return light ? 0xB91C1C : 0xF87171;
        case weather::AqiLevel::VeryUnhealthy:
            return light ? 0x9F1239 : 0xFB7185;
        case weather::AqiLevel::Hazardous:
            return light ? 0x7E22CE : 0xC084FC;
        case weather::AqiLevel::Unknown:
        default:
            return light ? 0x64748B : 0x94A3B8;
    }
}

static std::string zero_to_o(std::string_view text)
{
    std::string out(text);
    for (char& ch : out) {
        if (ch == '0') {
            ch = 'O';
        }
    }
    return out;
}

static void bind_delete_null(lv_obj_t* obj, lv_obj_t** slot)
{
    lv_obj_add_event_cb(
        obj,
        [](lv_event_t* e) {
            auto** ptr = static_cast<lv_obj_t**>(lv_event_get_user_data(e));
            if (ptr) {
                *ptr = nullptr;
            }
        },
        LV_EVENT_DELETE, slot);
}

void ArcTopClock::destroy_segment(ArcSegment& seg)
{
    if (seg.obj && lv_obj_is_valid(seg.obj)) {
        lv_obj_delete(seg.obj);
    }
    seg.obj = nullptr;
    seg.lastText.clear();
    seg.lastColor = 0;
}

ArcTopClock::~ArcTopClock()
{
    destroy_segment(_seg_aqi);
    destroy_segment(_seg_condition);
    destroy_segment(_seg_temp);
    destroy_segment(_seg_weekday);
    destroy_segment(_seg_time);
    destroy_segment(_seg_date);
}

lv_obj_t* ArcTopClock::create_arc_segment()
{
    lv_obj_t* arc = lv_arclabel_create(get());
    lv_obj_set_size(arc, 466, 466);
    lv_obj_center(arc);
    lv_arclabel_set_radius(arc, 214);
    lv_arclabel_set_dir(arc, LV_ARCLABEL_DIR_CLOCKWISE);
    lv_arclabel_set_text_horizontal_align(arc, LV_ARCLABEL_TEXT_ALIGN_LEADING);
    lv_arclabel_set_text_vertical_align(arc, LV_ARCLABEL_TEXT_ALIGN_CENTER);
    lv_arclabel_set_overflow(arc, LV_ARCLABEL_OVERFLOW_VISIBLE);
    lv_obj_set_style_text_font(arc, &lv_font_maple_mono_medium_24, LV_PART_MAIN);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
    return arc;
}

void ArcTopClock::init_full_date_arcs()
{
    setSize(466, 466);
    setBorderWidth(0);
    setOutlineWidth(0);
    setPaddingAll(0);
    setBgOpa(LV_OPA_TRANSP);
    removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    removeFlag(LV_OBJ_FLAG_CLICKABLE);
    addFlag(LV_OBJ_FLAG_EVENT_BUBBLE);
    addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    _seg_date.obj      = create_arc_segment();
    _seg_time.obj      = create_arc_segment();
    _seg_weekday.obj = create_arc_segment();
    _seg_temp.obj    = create_arc_segment();
    _seg_condition.obj = create_arc_segment();
    _seg_aqi.obj     = create_arc_segment();

    bind_delete_null(_seg_date.obj, &_seg_date.obj);
    bind_delete_null(_seg_time.obj, &_seg_time.obj);
    bind_delete_null(_seg_weekday.obj, &_seg_weekday.obj);
    bind_delete_null(_seg_temp.obj, &_seg_temp.obj);
    bind_delete_null(_seg_condition.obj, &_seg_condition.obj);
    bind_delete_null(_seg_aqi.obj, &_seg_aqi.obj);

    _last_theme = GetHAL().getThemeMode();
}

void ArcTopClock::set_segment(ArcSegment& seg, std::string_view text, uint32_t color, float angle_start,
                              float angle_size)
{
    if (seg.obj == nullptr) {
        return;
    }

    std::string transformed = zero_to_o(text);

    if (transformed.empty()) {
        if (!seg.lastText.empty()) {
            seg.lastText.clear();
            lv_arclabel_set_text(seg.obj, "");
            lv_obj_add_flag(seg.obj, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // Always refresh angle: weather segments shift when earlier fields appear/change length.
    lv_arclabel_set_angle_start(seg.obj, angle_start);
    lv_arclabel_set_angle_size(seg.obj, std::max(kDegPerChar, angle_size));

    if (transformed != seg.lastText) {
        seg.lastText = std::move(transformed);
        lv_arclabel_set_text(seg.obj, seg.lastText.c_str());
        lv_obj_remove_flag(seg.obj, LV_OBJ_FLAG_HIDDEN);
    }

    if (color != seg.lastColor) {
        seg.lastColor = color;
        lv_obj_set_style_text_color(seg.obj, lv_color_hex(color), LV_PART_MAIN);
    }
}

void ArcTopClock::init()
{
    if (showFullDate) {
        init_full_date_arcs();
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

void ArcTopClock::update_full_date(const std::tm* localTime)
{
    static const char* const kWeekDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const int wday = (localTime->tm_wday >= 0 && localTime->tm_wday < 7) ? localTime->tm_wday : 0;

    const auto theme_mode = GetHAL().getThemeMode();
    const auto palette    = palette_for_theme(theme_mode);

    const std::string date_text =
        fmt::format("{:04d}/{:02d}/{:02d}", localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday);
    const std::string time_text = fmt::format("{:02d}:{:02d}", localTime->tm_hour, localTime->tm_min);
    const std::string wday_text = kWeekDays[wday];

    set_segment(_seg_date, date_text, palette.date, kDateStart, date_text.size() * kDegPerChar);
    set_segment(_seg_time, time_text, palette.time, kTimeStart, time_text.size() * kDegPerChar);
    set_segment(_seg_weekday, wday_text, palette.weekday, kWeekdayStart, wday_text.size() * kDegPerChar);

    weather::tick();
    const auto snap = weather::snapshot();

    float cursor = kWeatherStart;
    if (snap.valid && !snap.temperature.empty()) {
        set_segment(_seg_temp, snap.temperature, palette.temp, cursor, snap.temperature.size() * kDegPerChar);
        cursor += (snap.temperature.size() + 1) * kDegPerChar;
    } else {
        set_segment(_seg_temp, "", palette.temp, cursor, kDegPerChar);
    }

    if (snap.valid && !snap.condition.empty()) {
        set_segment(_seg_condition, snap.condition, palette.condition, cursor,
                    snap.condition.size() * kDegPerChar);
        cursor += (snap.condition.size() + 1) * kDegPerChar;
    } else {
        set_segment(_seg_condition, "", palette.condition, cursor, kDegPerChar);
    }

    if (snap.valid && !snap.aqiText.empty()) {
        set_segment(_seg_aqi, snap.aqiText, aqi_color(snap.aqiLevel, theme_mode), cursor,
                    snap.aqiText.size() * kDegPerChar);
    } else {
        set_segment(_seg_aqi, "", aqi_color(weather::AqiLevel::Unknown, theme_mode), cursor, kDegPerChar);
    }

    _last_theme = theme_mode;
}

void ArcTopClock::update(bool force)
{
    if (force || GetHAL().millis() - update_time_count > updateInterval) {
        std::time_t now    = std::time(nullptr);
        std::tm* localTime = std::localtime(&now);
        if (localTime == nullptr) {
            return;
        }

        if (showFullDate) {
            // Theme may change while launcher stays open
            if (force || GetHAL().getThemeMode() != _last_theme) {
                // Force segment color refresh by clearing last colors
                _seg_date.lastColor      = 0xFFFFFFFF;
                _seg_time.lastColor      = 0xFFFFFFFF;
                _seg_weekday.lastColor   = 0xFFFFFFFF;
                _seg_temp.lastColor      = 0xFFFFFFFF;
                _seg_condition.lastColor = 0xFFFFFFFF;
                _seg_aqi.lastColor       = 0xFFFFFFFF;
            }
            update_full_date(localTime);
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

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "clock_faces.h"
#include <assets/assets.h>
#include <fmt/format.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cmath>

using namespace view;

namespace {

constexpr float kPi     = 3.14159265358979323846f;
constexpr float kCenter = 233.0f;

constexpr const char* kWeekShort[7]   = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
constexpr const char* kWeekLong[7]    = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
constexpr const char* kMonthShort[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// Zeller / Sakamoto weekday (0 = Sunday .. 6 = Saturday) directly from RTC DateYmd
int compute_weekday(uint16_t y, uint8_t m, uint8_t d)
{
    if (m < 1 || m > 12 || d < 1 || d > 31) {
        return 0;
    }
    static constexpr int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int yr                   = static_cast<int>(y) - (m < 3 ? 1 : 0);
    return (yr + yr / 4 - yr / 100 + yr / 400 + t[m - 1] + d) % 7;
}

uint32_t aqi_badge_color(weather::AqiLevel level, bool is_light)
{
    switch (level) {
    case weather::AqiLevel::Good:
        return is_light ? 0x2D6A4F : 0x34D399;
    case weather::AqiLevel::Moderate:
        return is_light ? 0xB07D2B : 0xFBBF24;
    case weather::AqiLevel::UnhealthySg:
        return is_light ? 0xC25927 : 0xFB923C;
    case weather::AqiLevel::Unhealthy:
        return is_light ? 0xB91C1C : 0xF87171;
    case weather::AqiLevel::VeryUnhealthy:
        return is_light ? 0x9F1239 : 0xFB7185;
    case weather::AqiLevel::Hazardous:
        return is_light ? 0x6B21A8 : 0xC084FC;
    case weather::AqiLevel::Unknown:
    default:
        return is_light ? 0x8A7F68 : 0x64748B;
    }
}

void make_passive(lv_obj_t* obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* make_plain_box(lv_obj_t* parent, int w, int h)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(o, 0, 0);
    make_passive(o);
    return o;
}

lv_obj_t* make_fullscreen_line(lv_obj_t* parent, int width)
{
    lv_obj_t* line = lv_line_create(parent);
    make_passive(line);
    lv_obj_set_size(line, 466, 466);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

void set_radial_line(lv_obj_t* line, lv_point_precise_t pts[2], float deg_from_top, float r_start, float r_end)
{
    const float rad = (deg_from_top - 90.0f) * (kPi / 180.0f);
    const float cs  = std::cos(rad);
    const float sn  = std::sin(rad);
#if LV_USE_FLOAT
    pts[0].x = kCenter + cs * r_start;
    pts[0].y = kCenter + sn * r_start;
    pts[1].x = kCenter + cs * r_end;
    pts[1].y = kCenter + sn * r_end;
#else
    pts[0].x = static_cast<lv_value_precise_t>(std::lround(kCenter + cs * r_start));
    pts[0].y = static_cast<lv_value_precise_t>(std::lround(kCenter + sn * r_start));
    pts[1].x = static_cast<lv_value_precise_t>(std::lround(kCenter + cs * r_end));
    pts[1].y = static_cast<lv_value_precise_t>(std::lround(kCenter + sn * r_end));
#endif
    lv_line_set_points(line, pts, 2);
}

}  // namespace

/* -------------------------------------------------------------------------- */
/*                                Lifecycle                                   */
/* -------------------------------------------------------------------------- */

ClockFacesView::ClockFacesView(lv_obj_t* parent)
{
    _last_theme       = GetHAL().getThemeMode();
    _active_face      = std::clamp(GetHAL().getClockFaceIndex(false), 0, kFaceCount - 1);
    _last_interact_ms = GetHAL().millis();

    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, 466, 466);
    lv_obj_set_pos(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_radius(_root, 0, 0);
    lv_obj_set_style_bg_color(_root, lv_color_hex(GetHAL().getTheme().background), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_root, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    // Vertical snap scroller for switching watch faces up/down while chaining horizontal swipes to outer pager
    _scroller = lv_obj_create(_root);
    lv_obj_set_size(_scroller, 466, 466);
    lv_obj_set_pos(_scroller, 0, 0);
    lv_obj_set_style_pad_all(_scroller, 0, 0);
    lv_obj_set_style_border_width(_scroller, 0, 0);
    lv_obj_set_style_radius(_scroller, 0, 0);
    lv_obj_set_style_bg_opa(_scroller, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(_scroller, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(_scroller, LV_SCROLL_SNAP_CENTER);
    lv_obj_add_flag(_scroller, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_add_flag(_scroller, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_scrollbar_mode(_scroller, LV_SCROLLBAR_MODE_OFF);

    lv_obj_add_event_cb(
        _scroller,
        [](lv_event_t* e) {
            auto* self = static_cast<ClockFacesView*>(lv_event_get_user_data(e));
            if (!self || !self->_scroller) {
                return;
            }
            const auto code = lv_event_get_code(e);
            if (code == LV_EVENT_SCROLL_BEGIN || code == LV_EVENT_SCROLL) {
                self->_last_interact_ms = GetHAL().millis();
                int sy                  = lv_obj_get_scroll_y(self->_scroller);
                int idx                 = std::clamp((sy + 233) / 466, 0, kFaceCount - 1);
                if (idx != self->_active_face) {
                    self->_active_face = idx;
                }
                self->refresh_indicator_visuals(true);
            } else if (code == LV_EVENT_SCROLL_END) {
                int sy                  = lv_obj_get_scroll_y(self->_scroller);
                int idx                 = std::clamp((sy + 233) / 466, 0, kFaceCount - 1);
                self->_active_face      = idx;
                self->_last_interact_ms = GetHAL().millis();
                GetHAL().setClockFaceIndex(idx, true);
                self->refresh_indicator_visuals(true);
            }
        },
        LV_EVENT_ALL, this);

    init_echo_time();
    init_echo_halo();
    init_echo_spot();
    init_bauhaus();
    init_indicators();

    applyTheme();
    if (_active_face > 0) {
        lv_obj_update_layout(_scroller);
        lv_obj_scroll_to_y(_scroller, _active_face * 466, LV_ANIM_OFF);
    }
    refresh_indicator_visuals(true);
    update(true);
}

ClockFacesView::~ClockFacesView()
{
    if (_root && lv_obj_is_valid(_root)) {
        lv_obj_delete(_root);
        _root = nullptr;
    }
}

void ClockFacesView::setFaceIndex(int index, bool animate)
{
    index        = std::clamp(index, 0, kFaceCount - 1);
    _active_face = index;
    GetHAL().setClockFaceIndex(index, true);
    if (_scroller) {
        lv_obj_scroll_to_y(_scroller, index * 466, animate ? LV_ANIM_ON : LV_ANIM_OFF);
    }
    _last_interact_ms = GetHAL().millis();
    refresh_indicator_visuals(true);
}

void ClockFacesView::nextFace()
{
    setFaceIndex((_active_face + 1) % kFaceCount, true);
}

void ClockFacesView::prevFace()
{
    setFaceIndex((_active_face + kFaceCount - 1) % kFaceCount, true);
}

/* -------------------------------------------------------------------------- */
/*                      Minimalist Vector Weather & Wind Icons                */
/* -------------------------------------------------------------------------- */

ClockFacesView::WeatherIconObjs ClockFacesView::create_weather_icon(lv_obj_t* parent, int /*scale_pct*/)
{
    WeatherIconObjs ic;
    ic.wrap = make_plain_box(parent, 24, 20);

    // Sun outer halo ring
    ic.sun_halo = make_plain_box(ic.wrap, 18, 18);
    lv_obj_set_style_radius(ic.sun_halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ic.sun_halo, 1, 0);
    lv_obj_align(ic.sun_halo, LV_ALIGN_CENTER, 0, 0);

    // Sun core orb
    ic.sun_core = make_plain_box(ic.wrap, 11, 11);
    lv_obj_set_style_radius(ic.sun_core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ic.sun_core, LV_OPA_COVER, 0);
    lv_obj_align(ic.sun_core, LV_ALIGN_CENTER, 0, 0);

    // Cloud puffs & base
    ic.cloud_l = make_plain_box(ic.wrap, 10, 10);
    lv_obj_set_style_radius(ic.cloud_l, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ic.cloud_l, LV_OPA_COVER, 0);
    lv_obj_set_pos(ic.cloud_l, 4, 3);

    ic.cloud_r = make_plain_box(ic.wrap, 8, 8);
    lv_obj_set_style_radius(ic.cloud_r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ic.cloud_r, LV_OPA_COVER, 0);
    lv_obj_set_pos(ic.cloud_r, 12, 5);

    ic.cloud_b = make_plain_box(ic.wrap, 18, 7);
    lv_obj_set_style_radius(ic.cloud_b, 4, 0);
    lv_obj_set_style_bg_opa(ic.cloud_b, LV_OPA_COVER, 0);
    lv_obj_set_pos(ic.cloud_b, 3, 7);

    // Rain / snow slanted droplets
    ic.drop1 = lv_line_create(ic.wrap);
    make_passive(ic.drop1);
    lv_obj_set_style_line_width(ic.drop1, 2, 0);
    lv_obj_set_style_line_rounded(ic.drop1, true, 0);
    ic.drop1_pts[0] = {9, 15};
    ic.drop1_pts[1] = {7, 19};
    lv_line_set_points(ic.drop1, ic.drop1_pts, 2);

    ic.drop2 = lv_line_create(ic.wrap);
    make_passive(ic.drop2);
    lv_obj_set_style_line_width(ic.drop2, 2, 0);
    lv_obj_set_style_line_rounded(ic.drop2, true, 0);
    ic.drop2_pts[0] = {15, 15};
    ic.drop2_pts[1] = {13, 19};
    lv_line_set_points(ic.drop2, ic.drop2_pts, 2);

    // Mist / haze bars
    ic.mist1 = make_plain_box(ic.wrap, 16, 2);
    lv_obj_set_style_radius(ic.mist1, 1, 0);
    lv_obj_set_style_bg_opa(ic.mist1, LV_OPA_COVER, 0);
    lv_obj_align(ic.mist1, LV_ALIGN_CENTER, -1, -3);

    ic.mist2 = make_plain_box(ic.wrap, 14, 2);
    lv_obj_set_style_radius(ic.mist2, 1, 0);
    lv_obj_set_style_bg_opa(ic.mist2, LV_OPA_COVER, 0);
    lv_obj_align(ic.mist2, LV_ALIGN_CENTER, 2, 3);

    return ic;
}

void ClockFacesView::update_weather_icon(WeatherIconObjs& ic, const std::string& cond, bool is_light)
{
    const uint32_t sun_col   = is_light ? 0xD9821E : 0xFBBF24;
    const uint32_t cloud_col = is_light ? 0x6B624C : 0xCBD5E1;
    const uint32_t rain_col  = is_light ? 0x2563EB : 0x38BDF8;
    const uint32_t snow_col  = is_light ? 0x0284C7 : 0x7DD3FC;
    const uint32_t mist_col  = is_light ? 0x8C7E63 : 0x94A3B8;

    lv_obj_set_style_bg_color(ic.sun_core, lv_color_hex(sun_col), 0);
    lv_obj_set_style_border_color(ic.sun_halo, lv_color_hex(sun_col), 0);
    lv_obj_set_style_border_opa(ic.sun_halo, LV_OPA_50, 0);

    lv_obj_set_style_bg_color(ic.cloud_l, lv_color_hex(cloud_col), 0);
    lv_obj_set_style_bg_color(ic.cloud_r, lv_color_hex(cloud_col), 0);
    lv_obj_set_style_bg_color(ic.cloud_b, lv_color_hex(cloud_col), 0);

    lv_obj_set_style_bg_color(ic.mist1, lv_color_hex(mist_col), 0);
    lv_obj_set_style_bg_color(ic.mist2, lv_color_hex(mist_col), 0);

    // Hide all first
    lv_obj_add_flag(ic.sun_halo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ic.sun_core, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ic.cloud_l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ic.cloud_r, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ic.cloud_b, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ic.drop1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ic.drop2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ic.mist1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ic.mist2, LV_OBJ_FLAG_HIDDEN);

    if (cond == "Rain" || cond == "Storm" || cond == "Hail") {
        lv_obj_remove_flag(ic.cloud_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.cloud_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.cloud_b, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.drop1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.drop2, LV_OBJ_FLAG_HIDDEN);
        uint32_t dcol = (cond == "Storm") ? sun_col : rain_col;
        lv_obj_set_style_line_color(ic.drop1, lv_color_hex(dcol), 0);
        lv_obj_set_style_line_color(ic.drop2, lv_color_hex(dcol), 0);
    } else if (cond == "Snow") {
        lv_obj_remove_flag(ic.cloud_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.cloud_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.cloud_b, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.drop1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.drop2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_line_color(ic.drop1, lv_color_hex(snow_col), 0);
        lv_obj_set_style_line_color(ic.drop2, lv_color_hex(snow_col), 0);
    } else if (cond == "Fog" || cond == "Haze" || cond == "Dust") {
        lv_obj_remove_flag(ic.mist1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.mist2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(ic.sun_core, 8, 8);
        lv_obj_align(ic.sun_core, LV_ALIGN_TOP_MID, 0, 1);
        lv_obj_remove_flag(ic.sun_core, LV_OBJ_FLAG_HIDDEN);
    } else if (cond == "Cloudy") {
        lv_obj_set_size(ic.sun_core, 9, 9);
        lv_obj_align(ic.sun_core, LV_ALIGN_TOP_LEFT, 12, 1);
        lv_obj_remove_flag(ic.sun_core, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.cloud_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.cloud_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.cloud_b, LV_OBJ_FLAG_HIDDEN);
    } else if (cond == "Overcast") {
        lv_obj_remove_flag(ic.cloud_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.cloud_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.cloud_b, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_size(ic.sun_core, 11, 11);
        lv_obj_align(ic.sun_core, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(ic.sun_halo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ic.sun_core, LV_OBJ_FLAG_HIDDEN);
    }
}

ClockFacesView::WindIconObjs ClockFacesView::create_wind_icon(lv_obj_t* parent)
{
    WindIconObjs w;
    w.wrap = make_plain_box(parent, 16, 12);

    w.line1 = make_plain_box(w.wrap, 13, 2);
    lv_obj_set_style_radius(w.line1, 1, 0);
    lv_obj_set_style_bg_opa(w.line1, LV_OPA_COVER, 0);
    lv_obj_set_pos(w.line1, 1, 3);

    w.curl = make_plain_box(w.wrap, 4, 4);
    lv_obj_set_style_radius(w.curl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(w.curl, 1, 0);
    lv_obj_set_pos(w.curl, 11, 0);

    w.line2 = make_plain_box(w.wrap, 10, 2);
    lv_obj_set_style_radius(w.line2, 1, 0);
    lv_obj_set_style_bg_opa(w.line2, LV_OPA_80, 0);
    lv_obj_set_pos(w.line2, 2, 8);

    return w;
}

void ClockFacesView::style_wind_icon(WindIconObjs& ic, uint32_t color)
{
    lv_obj_set_style_bg_color(ic.line1, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(ic.line2, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(ic.curl, lv_color_hex(color), 0);
}

/* -------------------------------------------------------------------------- */
/*          Face 0: Authentic Amazon Echo Time (Bold 12/3/6/9 Analog)         */
/* -------------------------------------------------------------------------- */

void ClockFacesView::init_echo_time()
{
    _echo.page = make_plain_box(_scroller, 466, 466);
    lv_obj_set_pos(_echo.page, 0, 0);
    lv_obj_set_style_bg_opa(_echo.page, LV_OPA_COVER, 0);

    auto make_num = [this](const char* txt, int dx, int dy) {
        lv_obj_t* lbl = lv_label_create(_echo.page);
        make_passive(lbl);
        lv_obj_set_style_text_font(lbl, &EchoNumeral136, 0);
        lv_obj_set_style_text_letter_space(lbl, -4, 0);
        lv_label_set_text(lbl, txt);
        lv_obj_align(lbl, LV_ALIGN_CENTER, dx, dy);
        return lbl;
    };

    // Full-bleed bold geometric cardinal numerals matching Amazon Echo Spot / Pop
    _echo.num_12 = make_num("12", -3, -146);
    _echo.num_3  = make_num("3", 150, 0);
    _echo.num_6  = make_num("6", 0, 146);
    _echo.num_9  = make_num("9", -150, 0);

    // Layer 1 (bottom hand): crisp cyan-blue second needle
    _echo.sec_hand = make_fullscreen_line(_echo.page, 3);

    // Layer 2 (middle hand): vibrant crimson-rose rounded capsule minute hand (odd width for exact center symmetry)
    _echo.min_hand = make_fullscreen_line(_echo.page, 25);

    // Layer 3 (top hand): soft pastel-pink rounded capsule hour hand with layer translucency (no double-blend seam)
    _echo.hour_hand = make_fullscreen_line(_echo.page, 39);
    lv_obj_set_style_opa(_echo.hour_hand, 218, 0);
}

/* -------------------------------------------------------------------------- */
/*              Face 1: Echo Halo (Full-Screen 60-Point Perimeter)            */
/* -------------------------------------------------------------------------- */

void ClockFacesView::init_echo_halo()
{
    _halo.page = make_plain_box(_scroller, 466, 466);
    lv_obj_set_pos(_halo.page, 0, 466);

    // Full-screen 60 radial ticks flush to the 466x466 circular perimeter (r = 233)
    for (int i = 0; i < 60; ++i) {
        const bool is_major = (i % 5 == 0);
        lv_obj_t* line      = make_fullscreen_line(_halo.page, is_major ? 3 : 2);
        const float r_in    = is_major ? 212.0f : 220.0f;
        const float r_out   = 230.0f;
        set_radial_line(line, _halo.tick_pts[i].data(), static_cast<float>(i) * 6.0f, r_in, r_out);
        _halo.ticks[i] = line;
    }

    // Active second luminous jewel + diffused halo riding along the outer perimeter ring
    _halo.sec_halo = make_plain_box(_halo.page, 16, 16);
    lv_obj_set_style_radius(_halo.sec_halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_halo.sec_halo, LV_OPA_30, 0);

    _halo.sec_dot = make_plain_box(_halo.page, 7, 7);
    lv_obj_set_style_radius(_halo.sec_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_halo.sec_dot, LV_OPA_COVER, 0);

    // Top date & weekday eyebrow
    _halo.date_label = lv_label_create(_halo.page);
    make_passive(_halo.date_label);
    lv_obj_set_style_text_font(_halo.date_label, &MontserratSemiBold18, 0);
    lv_obj_set_style_text_letter_space(_halo.date_label, 2, 0);
    lv_label_set_text(_halo.date_label, "TUE  /  NOV 24");
    lv_obj_align(_halo.date_label, LV_ALIGN_CENTER, 0, -124);

    _halo.rule_bar = make_plain_box(_halo.page, 32, 3);
    lv_obj_set_style_radius(_halo.rule_bar, 2, 0);
    lv_obj_set_style_bg_opa(_halo.rule_bar, LV_OPA_COVER, 0);
    lv_obj_align(_halo.rule_bar, LV_ALIGN_CENTER, 0, -92);

    // Center hero digital clock (clean HH:MM without redundant numeric seconds)
    _halo.time_label = lv_label_create(_halo.page);
    make_passive(_halo.time_label);
    lv_obj_set_style_text_font(_halo.time_label, &CommissionerMedium108, 0);
    lv_label_set_text(_halo.time_label, "00:00");
    lv_obj_align(_halo.time_label, LV_ALIGN_CENTER, 0, -4);

    // Bottom 3-zone Atmosphere Dock (Weather · Wind · AQI)
    _halo.dock = make_plain_box(_halo.page, 286, 44);
    lv_obj_set_style_radius(_halo.dock, 22, 0);
    lv_obj_set_style_bg_opa(_halo.dock, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_halo.dock, 1, 0);
    lv_obj_align(_halo.dock, LV_ALIGN_CENTER, 0, 108);

    // Zone 1: Weather icon + Temp
    lv_obj_t* z1 = make_plain_box(_halo.dock, 92, 42);
    lv_obj_align(z1, LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_set_flex_flow(z1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(z1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(z1, 4, 0);

    _halo.w_icon     = create_weather_icon(z1);
    _halo.temp_label = lv_label_create(z1);
    make_passive(_halo.temp_label);
    lv_obj_set_style_text_font(_halo.temp_label, &lv_font_montserrat_16, 0);
    lv_label_set_text(_halo.temp_label, "--\xC2\xB0" "C");

    _halo.div1 = make_plain_box(_halo.dock, 1, 16);
    lv_obj_set_style_bg_opa(_halo.div1, LV_OPA_COVER, 0);
    lv_obj_align(_halo.div1, LV_ALIGN_CENTER, -46, 0);

    // Zone 2: Wind icon + Wind dir/level
    lv_obj_t* z2 = make_plain_box(_halo.dock, 92, 42);
    lv_obj_align(z2, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(z2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(z2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(z2, 4, 0);

    _halo.wind_icon  = create_wind_icon(z2);
    _halo.wind_label = lv_label_create(z2);
    make_passive(_halo.wind_label);
    lv_obj_set_style_text_font(_halo.wind_label, &MontserratSemiBold16, 0);
    lv_label_set_text(_halo.wind_label, "Wind");

    _halo.div2 = make_plain_box(_halo.dock, 1, 16);
    lv_obj_set_style_bg_opa(_halo.div2, LV_OPA_COVER, 0);
    lv_obj_align(_halo.div2, LV_ALIGN_CENTER, 46, 0);

    // Zone 3: AQI jewel + AQI value
    lv_obj_t* z3 = make_plain_box(_halo.dock, 92, 42);
    lv_obj_align(z3, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_flex_flow(z3, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(z3, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(z3, 5, 0);

    _halo.aqi_dot = make_plain_box(z3, 7, 7);
    lv_obj_set_style_radius(_halo.aqi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_halo.aqi_dot, LV_OPA_COVER, 0);

    _halo.aqi_label = lv_label_create(z3);
    make_passive(_halo.aqi_label);
    lv_obj_set_style_text_font(_halo.aqi_label, &MontserratSemiBold16, 0);
    lv_label_set_text(_halo.aqi_label, "AQ --");
}

/* -------------------------------------------------------------------------- */
/*              Face 2: Echo Spot Horizon (Full-Screen Perimeter)             */
/* -------------------------------------------------------------------------- */

void ClockFacesView::init_echo_spot()
{
    _spot.page = make_plain_box(_scroller, 466, 466);
    lv_obj_set_pos(_spot.page, 0, 932);

    // Full-screen upper celestial horizon arc flush with the 466x466 edge
    _spot.sky_arc = lv_arc_create(_spot.page);
    make_passive(_spot.sky_arc);
    lv_obj_set_size(_spot.sky_arc, 462, 462);
    lv_obj_center(_spot.sky_arc);
    lv_arc_set_bg_angles(_spot.sky_arc, 204, 336);
    lv_arc_set_range(_spot.sky_arc, 0, 1440);
    lv_arc_set_value(_spot.sky_arc, 720);
    lv_obj_set_style_arc_width(_spot.sky_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(_spot.sky_arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(_spot.sky_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(_spot.sky_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_spot.sky_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(_spot.sky_arc, 0, LV_PART_KNOB);

    // Sculptural upper sky dome capsule concentric inside full-screen sky_arc
    _spot.dome = make_plain_box(_spot.page, 260, 90);
    lv_obj_set_style_radius(_spot.dome, 45, 0);
    lv_obj_set_style_bg_opa(_spot.dome, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_spot.dome, 1, 0);
    lv_obj_align(_spot.dome, LV_ALIGN_TOP_MID, 0, 38);

    // Dome Row 1: Weather icon + Condition & Temperature
    lv_obj_t* row1 = make_plain_box(_spot.dome, 232, 32);
    lv_obj_align(row1, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row1, 7, 0);

    _spot.w_icon        = create_weather_icon(row1);
    _spot.cond_temp_lbl = lv_label_create(row1);
    make_passive(_spot.cond_temp_lbl);
    lv_obj_set_style_text_font(_spot.cond_temp_lbl, &lv_font_montserrat_18, 0);
    lv_label_set_text(_spot.cond_temp_lbl, "Clear  --\xC2\xB0" "C");

    // Dome Row 2: Wind + AQI
    lv_obj_t* row2 = make_plain_box(_spot.dome, 232, 26);
    lv_obj_align(row2, LV_ALIGN_BOTTOM_MID, 0, -13);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row2, 5, 0);

    _spot.wind_icon = create_wind_icon(row2);
    _spot.wind_lbl  = lv_label_create(row2);
    make_passive(_spot.wind_lbl);
    lv_obj_set_style_text_font(_spot.wind_lbl, &MontserratSemiBold16, 0);
    lv_label_set_text(_spot.wind_lbl, "Wind --");

    _spot.meta_sep_dot = make_plain_box(row2, 3, 3);
    lv_obj_set_style_radius(_spot.meta_sep_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_spot.meta_sep_dot, LV_OPA_COVER, 0);

    _spot.aqi_dot = make_plain_box(row2, 7, 7);
    lv_obj_set_style_radius(_spot.aqi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_spot.aqi_dot, LV_OPA_COVER, 0);

    _spot.aqi_lbl = lv_label_create(row2);
    make_passive(_spot.aqi_lbl);
    lv_obj_set_style_text_font(_spot.aqi_lbl, &MontserratSemiBold16, 0);
    lv_label_set_text(_spot.aqi_lbl, "AQI --");

    // Echo Spot signature dual-tone split large numerals (clean HH:MM, no numeric seconds)
    _spot.hour_lbl = lv_label_create(_spot.page);
    make_passive(_spot.hour_lbl);
    lv_obj_set_width(_spot.hour_lbl, 148);
    lv_obj_set_style_text_align(_spot.hour_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(_spot.hour_lbl, &CommissionerMedium108, 0);
    lv_label_set_text(_spot.hour_lbl, "00");
    lv_obj_align(_spot.hour_lbl, LV_ALIGN_CENTER, -88, 6);

    _spot.colon_top = make_plain_box(_spot.page, 7, 7);
    lv_obj_set_style_radius(_spot.colon_top, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_spot.colon_top, LV_OPA_COVER, 0);
    lv_obj_align(_spot.colon_top, LV_ALIGN_CENTER, 0, -8);

    _spot.colon_bot = make_plain_box(_spot.page, 7, 7);
    lv_obj_set_style_radius(_spot.colon_bot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_spot.colon_bot, LV_OPA_COVER, 0);
    lv_obj_align(_spot.colon_bot, LV_ALIGN_CENTER, 0, 22);

    _spot.min_lbl = lv_label_create(_spot.page);
    make_passive(_spot.min_lbl);
    lv_obj_set_width(_spot.min_lbl, 148);
    lv_obj_set_style_text_align(_spot.min_lbl, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(_spot.min_lbl, &CommissionerMedium108, 0);
    lv_label_set_text(_spot.min_lbl, "00");
    lv_obj_align(_spot.min_lbl, LV_ALIGN_CENTER, 88, 6);

    // Lower weekday & date cleanly balanced above the bottom smile arc
    _spot.date_lbl = lv_label_create(_spot.page);
    make_passive(_spot.date_lbl);
    lv_obj_set_style_text_font(_spot.date_lbl, &MontserratSemiBold18, 0);
    lv_obj_set_style_text_letter_space(_spot.date_lbl, 2, 0);
    lv_label_set_text(_spot.date_lbl, "TUESDAY  /  NOV 24");
    lv_obj_align(_spot.date_lbl, LV_ALIGN_CENTER, 0, 120);

    // Full-screen bottom sweeping smile arc (fills left-to-right across 60 seconds)
    _spot.sec_arc = lv_arc_create(_spot.page);
    make_passive(_spot.sec_arc);
    lv_obj_set_size(_spot.sec_arc, 462, 462);
    lv_obj_center(_spot.sec_arc);
    lv_arc_set_bg_angles(_spot.sec_arc, 54, 126);
    lv_arc_set_mode(_spot.sec_arc, LV_ARC_MODE_REVERSE);
    lv_arc_set_range(_spot.sec_arc, 0, 59);
    lv_arc_set_value(_spot.sec_arc, 0);
    lv_obj_set_style_arc_width(_spot.sec_arc, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(_spot.sec_arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(_spot.sec_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(_spot.sec_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(_spot.sec_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(_spot.sec_arc, 0, LV_PART_KNOB);
}

/* -------------------------------------------------------------------------- */
/*        Face 3: Bauhaus Atelier (Full-Screen Max Bill / Nomos Dial)         */
/* -------------------------------------------------------------------------- */

void ClockFacesView::init_bauhaus()
{
    _bauhaus.page = make_plain_box(_scroller, 466, 466);
    lv_obj_set_pos(_bauhaus.page, 0, 1398);

    // Full-bleed enamel / obsidian dial surface filling the entire 466x466 circle
    _bauhaus.dial_plate = make_plain_box(_bauhaus.page, 466, 466);
    lv_obj_set_style_bg_opa(_bauhaus.dial_plate, LV_OPA_COVER, 0);
    lv_obj_center(_bauhaus.dial_plate);

    // Full-perimeter hairline bezel ring right at the circular screen boundary
    _bauhaus.outer_ring = make_plain_box(_bauhaus.page, 464, 464);
    lv_obj_set_style_radius(_bauhaus.outer_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(_bauhaus.outer_ring, 1, 0);
    lv_obj_center(_bauhaus.outer_ring);

    // 60 full-perimeter horological chapter indices reaching r = 229
    for (int i = 0; i < 60; ++i) {
        const bool is_cardinal = (i % 15 == 0);
        const bool is_hour     = (i % 5 == 0);
        const int w            = is_cardinal ? 3 : (is_hour ? 2 : 1);
        const float r_in       = is_cardinal ? 205.0f : (is_hour ? 213.0f : 220.0f);
        const float r_out      = 228.5f;
        lv_obj_t* line         = make_fullscreen_line(_bauhaus.page, w);
        set_radial_line(line, _bauhaus.tick_pts[i].data(), static_cast<float>(i) * 6.0f, r_in, r_out);
        _bauhaus.ticks[i] = line;
    }

    // Minimalist Arabic cardinal numerals 12 & 6 + horological wordmarks
    _bauhaus.num_12 = lv_label_create(_bauhaus.page);
    make_passive(_bauhaus.num_12);
    lv_obj_set_style_text_font(_bauhaus.num_12, &lv_font_maple_mono_medium_28, 0);
    lv_label_set_text(_bauhaus.num_12, "12");
    lv_obj_align(_bauhaus.num_12, LV_ALIGN_CENTER, 0, -174);

    _bauhaus.brand_lbl = lv_label_create(_bauhaus.page);
    make_passive(_bauhaus.brand_lbl);
    lv_obj_set_style_text_font(_bauhaus.brand_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_letter_space(_bauhaus.brand_lbl, 4, 0);
    lv_label_set_text(_bauhaus.brand_lbl, "ECHO ATELIER");
    lv_obj_align(_bauhaus.brand_lbl, LV_ALIGN_CENTER, 0, -130);

    _bauhaus.sub_lbl = lv_label_create(_bauhaus.page);
    make_passive(_bauhaus.sub_lbl);
    lv_obj_set_style_text_font(_bauhaus.sub_lbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_letter_space(_bauhaus.sub_lbl, 3, 0);
    lv_label_set_text(_bauhaus.sub_lbl, "CHRONOMETER");
    lv_obj_align(_bauhaus.sub_lbl, LV_ALIGN_CENTER, 0, 132);

    _bauhaus.num_6 = lv_label_create(_bauhaus.page);
    make_passive(_bauhaus.num_6);
    lv_obj_set_style_text_font(_bauhaus.num_6, &lv_font_maple_mono_medium_28, 0);
    lv_label_set_text(_bauhaus.num_6, "6");
    lv_obj_align(_bauhaus.num_6, LV_ALIGN_CENTER, 0, 174);

    // 9 o'clock recessed Atmosphere Complication Sub-Plate (Weather + Wind + AQI)
    _bauhaus.atmo_plate = make_plain_box(_bauhaus.page, 118, 78);
    lv_obj_set_style_radius(_bauhaus.atmo_plate, 12, 0);
    lv_obj_set_style_bg_opa(_bauhaus.atmo_plate, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_bauhaus.atmo_plate, 1, 0);
    lv_obj_align(_bauhaus.atmo_plate, LV_ALIGN_CENTER, -112, 0);

    lv_obj_t* r1 = make_plain_box(_bauhaus.atmo_plate, 108, 22);
    lv_obj_align(r1, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_flex_flow(r1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r1, 4, 0);
    _bauhaus.w_icon   = create_weather_icon(r1);
    _bauhaus.temp_lbl = lv_label_create(r1);
    make_passive(_bauhaus.temp_lbl);
    lv_obj_set_style_text_font(_bauhaus.temp_lbl, &lv_font_montserrat_16, 0);
    lv_label_set_text(_bauhaus.temp_lbl, "--\xC2\xB0" "C");

    _bauhaus.wind_lbl = lv_label_create(_bauhaus.atmo_plate);
    make_passive(_bauhaus.wind_lbl);
    lv_obj_set_style_text_font(_bauhaus.wind_lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(_bauhaus.wind_lbl, "Wind --");
    lv_obj_align(_bauhaus.wind_lbl, LV_ALIGN_CENTER, 0, 4);

    lv_obj_t* r3 = make_plain_box(_bauhaus.atmo_plate, 108, 18);
    lv_obj_align(r3, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_flex_flow(r3, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r3, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r3, 4, 0);
    _bauhaus.aqi_dot = make_plain_box(r3, 6, 6);
    lv_obj_set_style_radius(_bauhaus.aqi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_bauhaus.aqi_dot, LV_OPA_COVER, 0);
    _bauhaus.aqi_lbl = lv_label_create(r3);
    make_passive(_bauhaus.aqi_lbl);
    lv_obj_set_style_text_font(_bauhaus.aqi_lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(_bauhaus.aqi_lbl, "AQI --");

    // 3 o'clock chamfered Day-Date Window ("TUE | 24")
    _bauhaus.date_win = make_plain_box(_bauhaus.page, 94, 36);
    lv_obj_set_style_radius(_bauhaus.date_win, 8, 0);
    lv_obj_set_style_bg_opa(_bauhaus.date_win, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_bauhaus.date_win, 1, 0);
    lv_obj_align(_bauhaus.date_win, LV_ALIGN_CENTER, 118, 0);

    _bauhaus.wday_lbl = lv_label_create(_bauhaus.date_win);
    make_passive(_bauhaus.wday_lbl);
    lv_obj_set_style_text_font(_bauhaus.wday_lbl, &MontserratSemiBold16, 0);
    lv_label_set_text(_bauhaus.wday_lbl, "TUE");
    lv_obj_align(_bauhaus.wday_lbl, LV_ALIGN_LEFT_MID, 10, 0);

    _bauhaus.date_div = make_plain_box(_bauhaus.date_win, 1, 18);
    lv_obj_set_style_bg_opa(_bauhaus.date_div, LV_OPA_COVER, 0);
    lv_obj_align(_bauhaus.date_div, LV_ALIGN_CENTER, 6, 0);

    _bauhaus.mday_lbl = lv_label_create(_bauhaus.date_win);
    make_passive(_bauhaus.mday_lbl);
    lv_obj_set_style_text_font(_bauhaus.mday_lbl, &MontserratSemiBold16, 0);
    lv_label_set_text(_bauhaus.mday_lbl, "24");
    lv_obj_align(_bauhaus.mday_lbl, LV_ALIGN_RIGHT_MID, -10, 0);

    // Sculpted analog hands (full-screen line bounds so caps never clip)
    _bauhaus.hour_hand = make_fullscreen_line(_bauhaus.page, 7);
    _bauhaus.hour_lume = make_fullscreen_line(_bauhaus.page, 3);
    _bauhaus.min_hand  = make_fullscreen_line(_bauhaus.page, 5);
    _bauhaus.min_lume  = make_fullscreen_line(_bauhaus.page, 1);
    _bauhaus.sec_hand  = make_fullscreen_line(_bauhaus.page, 3);

    // Machined dual-layer center pinion cap
    _bauhaus.cap_outer = make_plain_box(_bauhaus.page, 15, 15);
    lv_obj_set_style_radius(_bauhaus.cap_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_bauhaus.cap_outer, LV_OPA_COVER, 0);
    lv_obj_center(_bauhaus.cap_outer);

    _bauhaus.cap_inner = make_plain_box(_bauhaus.page, 7, 7);
    lv_obj_set_style_radius(_bauhaus.cap_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(_bauhaus.cap_inner, LV_OPA_COVER, 0);
    lv_obj_center(_bauhaus.cap_inner);
}

/* -------------------------------------------------------------------------- */
/*                       Page & Edge Swipe Indicators                         */
/* -------------------------------------------------------------------------- */

void ClockFacesView::init_indicators()
{
    // Left vertical watch-face index pills (4 dots, auto-fades after interaction)
    _ind_wrap = make_plain_box(_root, 12, 68);
    lv_obj_align(_ind_wrap, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_flex_flow(_ind_wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_ind_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(_ind_wrap, 6, 0);

    for (int i = 0; i < kFaceCount; ++i) {
        lv_obj_t* d = make_plain_box(_ind_wrap, 4, 5);
        lv_obj_set_style_radius(d, 2, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        _ind_dots[i] = d;
    }

    // Subtle right-bezel pill hinting "swipe left for App Desktop"
    _right_hint = make_plain_box(_root, 4, 28);
    lv_obj_set_style_radius(_right_hint, 2, 0);
    lv_obj_set_style_bg_opa(_right_hint, LV_OPA_30, 0);
    lv_obj_align(_right_hint, LV_ALIGN_RIGHT_MID, -10, 0);
}

void ClockFacesView::refresh_indicator_visuals(bool active_interaction)
{
    const auto& theme   = GetHAL().getTheme();
    const bool is_light = (GetHAL().getThemeMode() == Hal::ThemeMode::Light);
    const uint32_t act  = is_light ? 0xD63B54 : 0x38BDF8;
    const uint32_t dim  = theme.textMuted;

    _ind_dimmed = !active_interaction;
    lv_obj_set_style_opa(_ind_wrap, active_interaction ? LV_OPA_90 : LV_OPA_TRANSP, 0);

    for (int i = 0; i < kFaceCount; ++i) {
        const bool sel = (i == _active_face);
        lv_obj_set_size(_ind_dots[i], 4, sel ? 16 : 5);
        lv_obj_set_style_bg_color(_ind_dots[i], lv_color_hex(sel ? act : dim), 0);
    }
    if (_right_hint) {
        lv_obj_set_style_bg_color(_right_hint, lv_color_hex(theme.textMuted), 0);
        lv_obj_set_style_opa(_right_hint, active_interaction ? LV_OPA_40 : LV_OPA_TRANSP, 0);
    }
}

/* -------------------------------------------------------------------------- */
/*                             Theme Application                              */
/* -------------------------------------------------------------------------- */

void ClockFacesView::applyTheme()
{
    const auto mode     = GetHAL().getThemeMode();
    const auto& theme   = GetHAL().getTheme();
    const bool is_light = (mode == Hal::ThemeMode::Light);
    _last_theme         = mode;

    lv_obj_set_style_bg_color(_root, lv_color_hex(theme.background), 0);

    /* ---- Face 0: Authentic Amazon Echo Time ---- */
    const uint32_t echo_bg       = is_light ? 0xF7EAEB : 0x160C10;
    const uint32_t echo_num_col  = is_light ? 0x6B1329 : 0xF5D5DC;
    const uint32_t echo_hour_col = is_light ? 0xF28E9C : 0xF79AA8;
    const uint32_t echo_min_col  = is_light ? 0xD63B54 : 0xE14B65;
    const uint32_t echo_sec_col  = is_light ? 0x1EA2D6 : 0x38BDF8;

    lv_obj_set_style_bg_color(_echo.page, lv_color_hex(echo_bg), 0);
    lv_obj_set_style_text_color(_echo.num_12, lv_color_hex(echo_num_col), 0);
    lv_obj_set_style_text_color(_echo.num_3, lv_color_hex(echo_num_col), 0);
    lv_obj_set_style_text_color(_echo.num_6, lv_color_hex(echo_num_col), 0);
    lv_obj_set_style_text_color(_echo.num_9, lv_color_hex(echo_num_col), 0);
    lv_obj_set_style_line_color(_echo.sec_hand, lv_color_hex(echo_sec_col), 0);
    lv_obj_set_style_line_color(_echo.min_hand, lv_color_hex(echo_min_col), 0);
    lv_obj_set_style_line_color(_echo.hour_hand, lv_color_hex(echo_hour_col), 0);

    /* ---- Face 1: Echo Halo ---- */
    const uint32_t halo_accent = is_light ? 0xD97757 : 0x2DD4BF;
    const uint32_t halo_tip    = is_light ? 0xC84B28 : 0x7DD3FC;

    lv_obj_set_style_bg_color(_halo.sec_halo, lv_color_hex(halo_accent), 0);
    lv_obj_set_style_bg_color(_halo.sec_dot, lv_color_hex(halo_tip), 0);
    lv_obj_set_style_text_color(_halo.date_label, lv_color_hex(theme.textSecondary), 0);
    lv_obj_set_style_bg_color(_halo.rule_bar, lv_color_hex(halo_accent), 0);
    lv_obj_set_style_text_color(_halo.time_label, lv_color_hex(theme.textPrimary), 0);

    lv_obj_set_style_bg_color(_halo.dock, lv_color_hex(theme.cardBackground), 0);
    lv_obj_set_style_border_color(_halo.dock, lv_color_hex(theme.border), 0);
    lv_obj_set_style_bg_color(_halo.div1, lv_color_hex(theme.border), 0);
    lv_obj_set_style_bg_color(_halo.div2, lv_color_hex(theme.border), 0);
    lv_obj_set_style_text_color(_halo.temp_label, lv_color_hex(theme.textPrimary), 0);
    style_wind_icon(_halo.wind_icon, theme.textSecondary);
    lv_obj_set_style_text_color(_halo.wind_label, lv_color_hex(theme.textSecondary), 0);

    /* ---- Face 2: Echo Spot Horizon ---- */
    const uint32_t spot_accent  = is_light ? 0xD97757 : 0x38BDF8;
    const uint32_t spot_sky_bg  = is_light ? 0xEFE3B4 : 0x10202B;
    const uint32_t spot_sky_bdr = is_light ? 0xDFCC92 : 0x1E3A4C;
    const uint32_t spot_arc_bg  = is_light ? 0xE2D29E : 0x1B2632;

    lv_obj_set_style_arc_color(_spot.sky_arc, lv_color_hex(spot_arc_bg), LV_PART_MAIN);
    lv_obj_set_style_arc_color(_spot.sky_arc, lv_color_hex(spot_accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_spot.dome, lv_color_hex(spot_sky_bg), 0);
    lv_obj_set_style_border_color(_spot.dome, lv_color_hex(spot_sky_bdr), 0);
    lv_obj_set_style_text_color(_spot.cond_temp_lbl, lv_color_hex(theme.textPrimary), 0);
    style_wind_icon(_spot.wind_icon, theme.textSecondary);
    lv_obj_set_style_text_color(_spot.wind_lbl, lv_color_hex(theme.textSecondary), 0);
    lv_obj_set_style_bg_color(_spot.meta_sep_dot, lv_color_hex(theme.textMuted), 0);

    lv_obj_set_style_text_color(_spot.hour_lbl, lv_color_hex(theme.textPrimary), 0);
    lv_obj_set_style_bg_color(_spot.colon_top, lv_color_hex(spot_accent), 0);
    lv_obj_set_style_bg_color(_spot.colon_bot, lv_color_hex(spot_accent), 0);
    lv_obj_set_style_text_color(_spot.min_lbl, lv_color_hex(spot_accent), 0);
    lv_obj_set_style_text_color(_spot.date_lbl, lv_color_hex(theme.textSecondary), 0);
    lv_obj_set_style_arc_color(_spot.sec_arc, lv_color_hex(spot_arc_bg), LV_PART_MAIN);
    lv_obj_set_style_arc_color(_spot.sec_arc, lv_color_hex(spot_accent), LV_PART_INDICATOR);

    /* ---- Face 3: Bauhaus Atelier ---- */
    const uint32_t dial_bg      = is_light ? 0xFAF3D8 : 0x0A0E13;
    const uint32_t sub_plate_bg = is_light ? 0xF3E6BC : 0x131922;
    const uint32_t vermilion    = is_light ? 0xD64933 : 0xFF6B4A;
    const uint32_t lume_col     = is_light ? 0xFCF4D4 : 0x101820;

    lv_obj_set_style_bg_color(_bauhaus.dial_plate, lv_color_hex(dial_bg), 0);
    lv_obj_set_style_border_color(_bauhaus.outer_ring, lv_color_hex(theme.border), 0);

    for (int i = 0; i < 60; ++i) {
        const bool is_hour = (i % 5 == 0);
        lv_obj_set_style_line_color(_bauhaus.ticks[i],
                                    lv_color_hex(is_hour ? theme.textPrimary : theme.textMuted), 0);
    }

    lv_obj_set_style_text_color(_bauhaus.num_12, lv_color_hex(theme.textPrimary), 0);
    lv_obj_set_style_text_color(_bauhaus.num_6, lv_color_hex(theme.textPrimary), 0);
    lv_obj_set_style_text_color(_bauhaus.brand_lbl, lv_color_hex(theme.textMuted), 0);
    lv_obj_set_style_text_color(_bauhaus.sub_lbl, lv_color_hex(theme.textMuted), 0);

    lv_obj_set_style_bg_color(_bauhaus.atmo_plate, lv_color_hex(sub_plate_bg), 0);
    lv_obj_set_style_border_color(_bauhaus.atmo_plate, lv_color_hex(theme.border), 0);
    lv_obj_set_style_text_color(_bauhaus.temp_lbl, lv_color_hex(theme.textPrimary), 0);
    lv_obj_set_style_text_color(_bauhaus.wind_lbl, lv_color_hex(theme.textSecondary), 0);

    lv_obj_set_style_bg_color(_bauhaus.date_win, lv_color_hex(sub_plate_bg), 0);
    lv_obj_set_style_border_color(_bauhaus.date_win, lv_color_hex(theme.border), 0);
    lv_obj_set_style_text_color(_bauhaus.wday_lbl, lv_color_hex(vermilion), 0);
    lv_obj_set_style_bg_color(_bauhaus.date_div, lv_color_hex(theme.border), 0);
    lv_obj_set_style_text_color(_bauhaus.mday_lbl, lv_color_hex(theme.textPrimary), 0);

    lv_obj_set_style_line_color(_bauhaus.hour_hand, lv_color_hex(theme.textPrimary), 0);
    lv_obj_set_style_line_color(_bauhaus.hour_lume, lv_color_hex(lume_col), 0);
    lv_obj_set_style_line_color(_bauhaus.min_hand, lv_color_hex(theme.textPrimary), 0);
    lv_obj_set_style_line_color(_bauhaus.min_lume, lv_color_hex(lume_col), 0);
    lv_obj_set_style_line_color(_bauhaus.sec_hand, lv_color_hex(vermilion), 0);
    lv_obj_set_style_bg_color(_bauhaus.cap_outer, lv_color_hex(theme.textPrimary), 0);
    lv_obj_set_style_bg_color(_bauhaus.cap_inner, lv_color_hex(vermilion), 0);

    refresh_indicator_visuals(!_ind_dimmed);
    _last_sec = -1;
    _last_weather_sig.clear();
}

/* -------------------------------------------------------------------------- */
/*                               Live Updates                                 */
/* -------------------------------------------------------------------------- */

void ClockFacesView::update(bool force)
{
    if (GetHAL().getThemeMode() != _last_theme) {
        applyTheme();
        force = true;
    }

    const uint32_t now_ms = GetHAL().millis();
    if (!_ind_dimmed && (now_ms - _last_interact_ms >= 2200) && !_scroller) {
        refresh_indicator_visuals(false);
    } else if (!_ind_dimmed && _scroller && !lv_obj_is_scrolling(_scroller) && (now_ms - _last_interact_ms >= 2200)) {
        refresh_indicator_visuals(false);
    }

    weather::tick();
    const auto w = weather::snapshot();
    const auto t = GetHAL().getTimeHms();
    const auto d = GetHAL().getDateYmd();

    std::string w_sig = fmt::format("{}|{}|{}|{}|{}", w.valid ? 1 : 0, w.tempDisplay, w.condition, w.windText,
                                    w.aqiValue);

    const bool sec_changed  = force || (static_cast<int>(t.second) != _last_sec);
    const bool min_changed  = force || (static_cast<int>(t.minute) != _last_min) ||
                              (static_cast<int>(t.hour) != _last_hour);
    const bool date_changed = force || (d.year != _last_year) || (d.month != _last_month) || (d.day != _last_day);
    const bool w_changed    = force || (w_sig != _last_weather_sig);

    if (!sec_changed && !min_changed && !date_changed && !w_changed) {
        return;
    }

    const int wday = compute_weekday(d.year, d.month, d.day);

    update_echo_time(t);
    update_echo_halo(t, d, wday, w, force || min_changed || date_changed || w_changed);
    update_echo_spot(t, d, wday, w, force || min_changed || date_changed || w_changed);
    update_bauhaus(t, d, wday, w, force || min_changed || date_changed || w_changed);

    _last_sec         = t.second;
    _last_min         = t.minute;
    _last_hour        = t.hour;
    _last_year        = d.year;
    _last_month       = d.month;
    _last_day         = d.day;
    _last_weather_sig = std::move(w_sig);
}

void ClockFacesView::update_echo_time(const TimeHms& t)
{
    const int sec = std::clamp(static_cast<int>(t.second), 0, 59);

    const float sec_deg  = static_cast<float>(sec) * 6.0f;
    const float min_deg  = static_cast<float>(t.minute) * 6.0f + static_cast<float>(sec) * 0.1f;
    const float hour_deg = static_cast<float>(t.hour % 12) * 30.0f + static_cast<float>(t.minute) * 0.5f;

    set_radial_line(_echo.sec_hand, _echo.sec_pts, sec_deg, 0.0f, 198.0f);
    set_radial_line(_echo.min_hand, _echo.min_pts, min_deg, 0.0f, 174.0f);
    set_radial_line(_echo.hour_hand, _echo.hour_pts, hour_deg, 0.0f, 102.0f);
}

void ClockFacesView::update_echo_halo(const TimeHms& t, const DateYmd& d, int wday, const weather::Snapshot& w,
                                      bool refresh_meta)
{
    const bool is_light        = (_last_theme == Hal::ThemeMode::Light);
    const uint32_t lit_major   = is_light ? 0xC25E3A : 0x2DD4BF;
    const uint32_t lit_minor   = is_light ? 0xDF8E6E : 0x1E8E82;
    const uint32_t cur_col     = is_light ? 0xB83B1D : 0x7DD3FC;
    const uint32_t unlit_major = is_light ? 0xC8B788 : 0x334155;
    const uint32_t unlit_minor = is_light ? 0xE2D3A4 : 0x1C2430;

    const int sec = std::clamp(static_cast<int>(t.second), 0, 59);
    for (int i = 0; i < 60; ++i) {
        const bool major = (i % 5 == 0);
        uint32_t col     = 0;
        if (i == sec) {
            col = cur_col;
        } else if (i < sec) {
            col = major ? lit_major : lit_minor;
        } else {
            col = major ? unlit_major : unlit_minor;
        }
        lv_obj_set_style_line_color(_halo.ticks[i], lv_color_hex(col), 0);
    }

    // Move luminous second jewel along full-perimeter r = 221.0
    const float rad = (static_cast<float>(sec) * 6.0f - 90.0f) * (kPi / 180.0f);
    const int cx    = static_cast<int>(std::lround(kCenter + std::cos(rad) * 221.0f));
    const int cy    = static_cast<int>(std::lround(kCenter + std::sin(rad) * 221.0f));
    lv_obj_set_pos(_halo.sec_halo, cx - 8, cy - 8);
    lv_obj_set_pos(_halo.sec_dot, cx - 3, cy - 3);

    if (refresh_meta) {
        lv_label_set_text(_halo.time_label, fmt::format("{:02d}:{:02d}", t.hour, t.minute).c_str());

        const int m_idx = std::clamp(static_cast<int>(d.month) - 1, 0, 11);
        lv_label_set_text(_halo.date_label,
                          fmt::format("{}  /  {} {:02d}", kWeekShort[wday], kMonthShort[m_idx], d.day).c_str());

        update_weather_icon(_halo.w_icon, w.valid ? w.condition : "Clear", is_light);
        lv_label_set_text(_halo.temp_label,
                          (w.valid && !w.tempDisplay.empty()) ? w.tempDisplay.c_str() : "--\xC2\xB0" "C");

        std::string short_wind = "Wind";
        if (w.valid && !w.windDir.empty()) {
            short_wind = w.windDir;
            if (!w.windLevel.empty() && w.windDir != "Calm") {
                std::string lv_short = w.windLevel;
                if (lv_short.rfind("Lv.", 0) == 0) {
                    lv_short = "L" + lv_short.substr(3);
                }
                short_wind += " " + lv_short;
            }
        } else if (w.valid && !w.windLevel.empty()) {
            short_wind = w.windLevel;
        }
        lv_label_set_text(_halo.wind_label, short_wind.c_str());

        const uint32_t acol = aqi_badge_color(w.valid ? w.aqiLevel : weather::AqiLevel::Unknown, is_light);
        lv_obj_set_style_bg_color(_halo.aqi_dot, lv_color_hex(acol), 0);
        lv_obj_set_style_text_color(_halo.aqi_label, lv_color_hex(acol), 0);
        if (w.valid && w.aqiValue >= 0) {
            lv_label_set_text(_halo.aqi_label, fmt::format("AQ {}", w.aqiValue).c_str());
        } else {
            lv_label_set_text(_halo.aqi_label, "AQ --");
        }
    }
}

void ClockFacesView::update_echo_spot(const TimeHms& t, const DateYmd& d, int wday, const weather::Snapshot& w,
                                      bool refresh_meta)
{
    const bool is_light = (_last_theme == Hal::ThemeMode::Light);
    const int sec       = std::clamp(static_cast<int>(t.second), 0, 59);

    lv_arc_set_value(_spot.sec_arc, sec);

    // Gentle breathing colon pulse on alternate seconds
    const lv_opa_t pulse = (sec % 2 == 0) ? LV_OPA_COVER : LV_OPA_40;
    lv_obj_set_style_bg_opa(_spot.colon_top, pulse, 0);
    lv_obj_set_style_bg_opa(_spot.colon_bot, pulse, 0);

    if (refresh_meta) {
        lv_label_set_text(_spot.hour_lbl, fmt::format("{:02d}", t.hour).c_str());
        lv_label_set_text(_spot.min_lbl, fmt::format("{:02d}", t.minute).c_str());
        lv_arc_set_value(_spot.sky_arc, static_cast<int>(t.hour) * 60 + static_cast<int>(t.minute));

        const int m_idx = std::clamp(static_cast<int>(d.month) - 1, 0, 11);
        lv_label_set_text(_spot.date_lbl,
                          fmt::format("{}  /  {} {:02d}", kWeekLong[wday], kMonthShort[m_idx], d.day).c_str());

        update_weather_icon(_spot.w_icon, w.valid ? w.condition : "Clear", is_light);
        if (w.valid) {
            const std::string cond = w.condition.empty() ? "Weather" : w.condition;
            const std::string tmp  = w.tempDisplay.empty() ? "--\xC2\xB0" "C" : w.tempDisplay;
            lv_label_set_text(_spot.cond_temp_lbl, fmt::format("{}  {}", cond, tmp).c_str());
        } else {
            lv_label_set_text(_spot.cond_temp_lbl, "Syncing  --\xC2\xB0" "C");
        }

        if (w.valid && !w.windText.empty()) {
            lv_label_set_text(_spot.wind_lbl, w.windText.c_str());
        } else {
            lv_label_set_text(_spot.wind_lbl, "Wind --");
        }

        const uint32_t acol = aqi_badge_color(w.valid ? w.aqiLevel : weather::AqiLevel::Unknown, is_light);
        lv_obj_set_style_bg_color(_spot.aqi_dot, lv_color_hex(acol), 0);
        lv_obj_set_style_text_color(_spot.aqi_lbl, lv_color_hex(acol), 0);
        if (w.valid && w.aqiValue >= 0) {
            lv_label_set_text(_spot.aqi_lbl, fmt::format("AQI {}", w.aqiValue).c_str());
        } else {
            lv_label_set_text(_spot.aqi_lbl, "AQI --");
        }
    }
}

void ClockFacesView::update_bauhaus(const TimeHms& t, const DateYmd& d, int wday, const weather::Snapshot& w,
                                    bool refresh_meta)
{
    const bool is_light = (_last_theme == Hal::ThemeMode::Light);
    const int sec       = std::clamp(static_cast<int>(t.second), 0, 59);

    const float sec_deg  = static_cast<float>(sec) * 6.0f;
    const float min_deg  = static_cast<float>(t.minute) * 6.0f + static_cast<float>(sec) * 0.1f;
    const float hour_deg = static_cast<float>(t.hour % 12) * 30.0f + static_cast<float>(t.minute) * 0.5f;

    set_radial_line(_bauhaus.hour_hand, _bauhaus.hour_pts, hour_deg, -20.0f, 118.0f);
    set_radial_line(_bauhaus.hour_lume, _bauhaus.hour_lume_pts, hour_deg, 30.0f, 105.0f);

    set_radial_line(_bauhaus.min_hand, _bauhaus.min_pts, min_deg, -26.0f, 180.0f);
    set_radial_line(_bauhaus.min_lume, _bauhaus.min_lume_pts, min_deg, 38.0f, 165.0f);

    set_radial_line(_bauhaus.sec_hand, _bauhaus.sec_pts, sec_deg, -36.0f, 204.0f);

    if (refresh_meta) {
        lv_label_set_text(_bauhaus.wday_lbl, kWeekShort[wday]);
        lv_label_set_text(_bauhaus.mday_lbl, fmt::format("{:02d}", d.day).c_str());

        update_weather_icon(_bauhaus.w_icon, w.valid ? w.condition : "Clear", is_light);
        lv_label_set_text(_bauhaus.temp_lbl,
                          (w.valid && !w.tempDisplay.empty()) ? w.tempDisplay.c_str() : "--\xC2\xB0" "C");

        if (w.valid && !w.windText.empty()) {
            lv_label_set_text(_bauhaus.wind_lbl, w.windText.c_str());
        } else {
            lv_label_set_text(_bauhaus.wind_lbl, "Wind --");
        }

        const uint32_t acol = aqi_badge_color(w.valid ? w.aqiLevel : weather::AqiLevel::Unknown, is_light);
        lv_obj_set_style_bg_color(_bauhaus.aqi_dot, lv_color_hex(acol), 0);
        lv_obj_set_style_text_color(_bauhaus.aqi_lbl, lv_color_hex(acol), 0);
        if (w.valid && w.aqiValue >= 0) {
            lv_label_set_text(_bauhaus.aqi_lbl, fmt::format("AQI {}", w.aqiValue).c_str());
        } else {
            lv_label_set_text(_bauhaus.aqi_lbl, "AQI --");
        }
    }
}

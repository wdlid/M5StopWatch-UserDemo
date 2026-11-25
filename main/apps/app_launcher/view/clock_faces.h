/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/weather/weather.h>
#include <hal/hal.h>
#include <lvgl.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace view {

/**
 * @brief Left-screen full-circle (466x466) Echo-inspired clock face carousel.
 *
 * Supports vertical snap-scrolling between 4 curated full-bleed watch faces:
 *   0: Echo Time       — Iconic full-screen blush dial + bold 12/3/6/9 + dual-tone capsule hands + cyan second needle
 *   1: Echo Halo       — Full-bleed 60-point luminous perimeter second ring + digital HH:MM + atmosphere dock
 *   2: Echo Spot       — Full-bleed solar horizon & second arcs + dual-tone split numerals + atmosphere dome
 *   3: Bauhaus Atelier — Full-bleed 60-index horological dial + 9H weather/wind/AQI plate + 3H day-date window
 */
class ClockFacesView {
public:
    static constexpr int kFaceCount = 4;

    explicit ClockFacesView(lv_obj_t* parent);
    ~ClockFacesView();

    ClockFacesView(const ClockFacesView&)            = delete;
    ClockFacesView& operator=(const ClockFacesView&) = delete;

    void update(bool force = false);
    void applyTheme();
    void setPagerScrolling(bool scrolling);

    void setFaceIndex(int index, bool animate);
    int getFaceIndex() const
    {
        return _active_face;
    }
    void nextFace();
    void prevFace();

    lv_obj_t* root() const
    {
        return _root;
    }

private:
    struct WeatherIconObjs {
        lv_obj_t* wrap      = nullptr;
        lv_obj_t* sun_halo  = nullptr;
        lv_obj_t* sun_core  = nullptr;
        lv_obj_t* cloud_b   = nullptr;
        lv_obj_t* cloud_l   = nullptr;
        lv_obj_t* cloud_r   = nullptr;
        lv_obj_t* drop1     = nullptr;
        lv_obj_t* drop2     = nullptr;
        lv_obj_t* mist1     = nullptr;
        lv_obj_t* mist2     = nullptr;
        lv_point_precise_t drop1_pts[2]{};
        lv_point_precise_t drop2_pts[2]{};
    };

    struct WindIconObjs {
        lv_obj_t* wrap  = nullptr;
        lv_obj_t* line1 = nullptr;
        lv_obj_t* line2 = nullptr;
        lv_obj_t* curl  = nullptr;
    };

    // Face 0: Authentic Amazon Echo Time Analog
    struct EchoTimeFace {
        lv_obj_t* page      = nullptr;
        lv_obj_t* num_12    = nullptr;
        lv_obj_t* num_3     = nullptr;
        lv_obj_t* num_6     = nullptr;
        lv_obj_t* num_9     = nullptr;
        lv_obj_t* sec_hand  = nullptr;
        lv_obj_t* min_hand  = nullptr;
        lv_obj_t* hour_hand = nullptr;
        lv_point_precise_t sec_pts[2]{};
        lv_point_precise_t min_pts[2]{};
        lv_point_precise_t hour_pts[2]{};
    };

    // Face 1: Echo Halo (Full-Screen 60-Point Perimeter)
    struct EchoHaloFace {
        lv_obj_t* page = nullptr;
        std::array<lv_obj_t*, 60> ticks{};
        std::array<std::array<lv_point_precise_t, 2>, 60> tick_pts{};
        lv_obj_t* sec_halo   = nullptr;
        lv_obj_t* sec_dot    = nullptr;
        lv_obj_t* date_label = nullptr;
        lv_obj_t* rule_bar   = nullptr;
        lv_obj_t* time_label = nullptr;
        lv_obj_t* dock       = nullptr;
        lv_obj_t* div1       = nullptr;
        lv_obj_t* div2       = nullptr;
        WeatherIconObjs w_icon;
        lv_obj_t* temp_label = nullptr;
        WindIconObjs wind_icon;
        lv_obj_t* wind_label = nullptr;
        lv_obj_t* aqi_dot    = nullptr;
        lv_obj_t* aqi_label  = nullptr;
    };

    // Face 2: Echo Spot Horizon
    struct EchoSpotFace {
        lv_obj_t* page          = nullptr;
        lv_obj_t* sky_arc       = nullptr;
        lv_obj_t* dome          = nullptr;
        WeatherIconObjs w_icon;
        lv_obj_t* cond_temp_lbl = nullptr;
        WindIconObjs wind_icon;
        lv_obj_t* wind_lbl      = nullptr;
        lv_obj_t* meta_sep_dot  = nullptr;
        lv_obj_t* aqi_dot       = nullptr;
        lv_obj_t* aqi_lbl       = nullptr;
        lv_obj_t* hour_lbl      = nullptr;
        lv_obj_t* colon_top     = nullptr;
        lv_obj_t* colon_bot     = nullptr;
        lv_obj_t* min_lbl       = nullptr;
        lv_obj_t* date_lbl      = nullptr;
        lv_obj_t* sec_arc       = nullptr;
    };

    // Face 3: Bauhaus Atelier
    struct BauhausFace {
        lv_obj_t* page       = nullptr;
        lv_obj_t* outer_ring = nullptr;
        std::array<lv_obj_t*, 60> ticks{};
        std::array<std::array<lv_point_precise_t, 2>, 60> tick_pts{};
        lv_obj_t* num_12     = nullptr;
        lv_obj_t* brand_lbl  = nullptr;
        lv_obj_t* sub_lbl    = nullptr;
        lv_obj_t* num_6      = nullptr;
        lv_obj_t* atmo_plate = nullptr;
        WeatherIconObjs w_icon;
        lv_obj_t* temp_lbl   = nullptr;
        lv_obj_t* wind_lbl   = nullptr;
        lv_obj_t* aqi_dot    = nullptr;
        lv_obj_t* aqi_lbl    = nullptr;
        lv_obj_t* date_win   = nullptr;
        lv_obj_t* wday_lbl   = nullptr;
        lv_obj_t* date_div   = nullptr;
        lv_obj_t* mday_lbl   = nullptr;
        lv_obj_t* hour_hand  = nullptr;
        lv_obj_t* hour_lume  = nullptr;
        lv_obj_t* min_hand   = nullptr;
        lv_obj_t* min_lume   = nullptr;
        lv_obj_t* sec_hand   = nullptr;
        lv_obj_t* cap_outer  = nullptr;
        lv_obj_t* cap_inner  = nullptr;
        lv_point_precise_t hour_pts[2]{};
        lv_point_precise_t hour_lume_pts[2]{};
        lv_point_precise_t min_pts[2]{};
        lv_point_precise_t min_lume_pts[2]{};
        lv_point_precise_t sec_pts[2]{};
    };

    void init_echo_time();
    void init_echo_halo();
    void init_echo_spot();
    void init_bauhaus();
    void init_indicators();

    static WeatherIconObjs create_weather_icon(lv_obj_t* parent, int scale_pct = 100);
    static void update_weather_icon(WeatherIconObjs& ic, const std::string& condition, bool is_light);
    static WindIconObjs create_wind_icon(lv_obj_t* parent);
    static void style_wind_icon(WindIconObjs& ic, uint32_t color);

    void update_echo_time(const TimeHms& t);
    void update_echo_halo(const TimeHms& t, const DateYmd& d, int wday, const weather::Snapshot& w, bool force);
    void update_echo_spot(const TimeHms& t, const DateYmd& d, int wday, const weather::Snapshot& w, bool force);
    void update_bauhaus(const TimeHms& t, const DateYmd& d, int wday, const weather::Snapshot& w, bool force);
    void update_single_face(int idx, const TimeHms& t, const DateYmd& d, int wday, const weather::Snapshot& w,
                            bool refresh_meta);
    void flush_dirty_faces_for_scroll();
    void update_scroll_dir_bounds();
    void refresh_indicator_visuals(bool active_interaction);

    lv_obj_t* _root     = nullptr;
    lv_obj_t* _scroller = nullptr;
    std::array<lv_obj_t*, kFaceCount> _ind_dots{};
    lv_obj_t* _ind_wrap   = nullptr;
    lv_obj_t* _right_hint = nullptr;

    EchoTimeFace _echo;
    EchoHaloFace _halo;
    EchoSpotFace _spot;
    BauhausFace _bauhaus;

    int _active_face            = 0;
    uint32_t _last_interact_ms  = 0;
    uint32_t _last_poll_ms      = 0;
    bool _ind_dimmed            = false;
    bool _vert_scrolling        = false;
    bool _pager_scrolling       = false;
    std::array<bool, kFaceCount> _face_sec_dirty{};
    std::array<bool, kFaceCount> _face_meta_dirty{};

    int _last_sec              = -1;
    int _last_min              = -1;
    int _last_hour             = -1;
    uint16_t _last_year        = 0;
    uint8_t _last_month        = 0;
    uint8_t _last_day          = 0;
    weather::Snapshot _cached_weather;
    std::string _last_weather_sig;
    Hal::ThemeMode _last_theme = Hal::ThemeMode::Light;
};

}  // namespace view

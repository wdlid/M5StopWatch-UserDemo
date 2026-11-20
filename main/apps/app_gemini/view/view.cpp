/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

using namespace view;
using namespace uitk::lvgl_cpp;

void mist_draw_event_cb(lv_event_t* e);

constexpr int _panel_size = 466;
constexpr int _canvas_size = 220;
constexpr int _num_sectors = 24;
constexpr int _deg_per_sector = 360 / _num_sectors;

int32_t norm_deg(int32_t deg)
{
    deg %= 360;
    if (deg < 0) {
        deg += 360;
    }
    return deg;
}

int32_t get_cos(int32_t deg, int32_t amplitude)
{
    deg = norm_deg(deg);
    int32_t result = lv_trigo_cos(deg) * amplitude;
    result += LV_TRIGO_SIN_MAX / 2;
    return result >> LV_TRIGO_SHIFT;
}

int32_t get_sin(int32_t deg, int32_t amplitude)
{
    deg = norm_deg(deg);
    int32_t result = lv_trigo_sin(deg) * amplitude;
    return (result + LV_TRIGO_SIN_MAX / 2) >> LV_TRIGO_SHIFT;
}

lv_color_t get_sector_color(int32_t deg, int32_t rot, gemini::SessionState state,
                             bool is_talking, bool web_active, float pitch)
{
    if (web_active) {
        int32_t a = norm_deg(deg + rot);
        uint8_t m = static_cast<uint8_t>((get_cos(a * 2, 127) + 128));
        return lv_color_mix(lv_color_hex(0xA855F7), lv_color_hex(0x7C3AED), m);
    }

    if (state == gemini::SessionState::Error) {
        return lv_color_hex(0xDC2626);
    }

    if (is_talking || state == gemini::SessionState::Listening) {
        // High-energy electric cyan & turquoise reacting to voice pitch
        int32_t a = norm_deg(deg * 2 - rot);
        uint8_t m = static_cast<uint8_t>((get_sin(a, 100) + 128));
        uint8_t pitch_mix = static_cast<uint8_t>(std::clamp(pitch * 200.0f, 0.0f, 255.0f));
        lv_color_t base = lv_color_mix(lv_color_hex(0x22D3EE), lv_color_hex(0x06B6D4), m);
        return lv_color_mix(lv_color_hex(0x67E8F9), base, pitch_mix);
    }

    if (state == gemini::SessionState::Thinking) {
        // Mystic violet & purple nebula swirl
        int32_t a = norm_deg(deg * 3 + rot * 2);
        uint8_t m = static_cast<uint8_t>((get_cos(a, 120) + 128));
        return lv_color_mix(lv_color_hex(0x9333EA), lv_color_hex(0x6366F1), m);
    }

    if (state == gemini::SessionState::Speaking) {
        // Iconic Google 4-Color rotating gradient: Blue -> Violet -> Coral -> Amber
        int32_t a = norm_deg(deg + rot);
        int quadrant = a / 90;
        int rem = a % 90;
        uint8_t frac = static_cast<uint8_t>((rem * 255) / 90);

        switch (quadrant) {
            case 0:
                return lv_color_mix(lv_color_hex(0x8B5CF6), lv_color_hex(0x3B82F6), frac);
            case 1:
                return lv_color_mix(lv_color_hex(0xEF4444), lv_color_hex(0x8B5CF6), frac);
            case 2:
                return lv_color_mix(lv_color_hex(0xF59E0B), lv_color_hex(0xEF4444), frac);
            default:
                return lv_color_mix(lv_color_hex(0x3B82F6), lv_color_hex(0xF59E0B), frac);
        }
    }

    // Default / Ready / Idle: Calm cosmic azure
    int32_t a = norm_deg(deg + rot);
    uint8_t m = static_cast<uint8_t>((get_cos(a, 100) + 128));
    return lv_color_mix(lv_color_hex(0x0EA5E9), lv_color_hex(0x0284C7), m);
}

}  // namespace

namespace view {

void GeminiView::init(lv_obj_t* parent)
{
    // Fullscreen dark cosmic container (466x466 circular screen)
    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(_panel_size, _panel_size);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgColor(lv_color_hex(0x05070D));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    // Dynamic living visual canvas in screen center (220x220)
    _mist_canvas = std::make_unique<Container>(_panel->get());
    _mist_canvas->align(LV_ALIGN_CENTER, 0, 0);
    _mist_canvas->setSize(_canvas_size, _canvas_size);
    _mist_canvas->setRadius(0);
    _mist_canvas->setBorderWidth(0);
    _mist_canvas->setPaddingAll(0);
    _mist_canvas->setBgColor(lv_color_hex(0x05070D));
    _mist_canvas->setBgOpa(LV_OPA_COVER);
    _mist_canvas->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _mist_canvas->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(_mist_canvas->get(), mist_draw_event_cb, LV_EVENT_ALL, this);
    lv_obj_refresh_ext_draw_size(_mist_canvas->get());

    // Center interactive touch target for pattern hold-to-talk
    _pattern_touch = std::make_unique<Container>(_panel->get());
    _pattern_touch->align(LV_ALIGN_CENTER, 0, 0);
    _pattern_touch->setSize(340, 340);
    _pattern_touch->setRadius(LV_RADIUS_CIRCLE);
    _pattern_touch->setBorderWidth(0);
    _pattern_touch->setPaddingAll(0);
    _pattern_touch->setBgOpa(LV_OPA_TRANSP);
    _pattern_touch->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _pattern_touch->addFlag(LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(
        _pattern_touch->get(),
        [](lv_event_t* e) {
            auto* view = static_cast<GeminiView*>(lv_event_get_user_data(e));
            if (!view) {
                return;
            }
            lv_event_code_t code = lv_event_get_code(e);
            if (code == LV_EVENT_PRESSED) {
                view->_pattern_pressed = true;
                if (view->isWebConfigActive()) {
                    if (view->onExitWebConfigClicked) {
                        view->onExitWebConfigClicked();
                    }
                } else if (view->onPatternPressedChanged) {
                    view->onPatternPressedChanged(true);
                }
            } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
                if (view->_pattern_pressed) {
                    view->_pattern_pressed = false;
                    if (!view->isWebConfigActive() && view->onPatternPressedChanged) {
                        view->onPatternPressedChanged(false);
                    }
                }
            } else if (code == LV_EVENT_CLICKED) {
                if (view->onPatternClicked) {
                    view->onPatternClicked();
                }
            }
        },
        LV_EVENT_ALL, this);

    // Minimal floating status indicator at top
    _status_label = std::make_unique<Label>(_panel->get());
    _status_label->setTextFont(&lv_font_misans_18);
    _status_label->setTextColor(lv_color_hex(0x94A3B8));
    _status_label->setWidth(380);
    _status_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _status_label->setText("Connecting...");
    _status_label->align(LV_ALIGN_TOP_MID, 0, 36);

    // Subtitle transcript at bottom (no buttons!)
    _transcript_label = std::make_unique<Label>(_panel->get());
    _transcript_label->setTextFont(&lv_font_misans_18);
    _transcript_label->setTextColor(lv_color_hex(0xE2E8F0));
    _transcript_label->setWidth(380);
    _transcript_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _transcript_label->setLongMode(LV_LABEL_LONG_MODE_WRAP);
    _transcript_label->setText("Hold Btn B or Pattern to talk");
    _transcript_label->align(LV_ALIGN_BOTTOM_MID, 0, -32);

    // Web Config overlay card (only shown in Web Setup mode)
    _web_box = std::make_unique<Container>(_panel->get());
    _web_box->align(LV_ALIGN_CENTER, 0, 0);
    _web_box->setSize(360, 200);
    _web_box->setRadius(24);
    _web_box->setBgColor(lv_color_hex(0x131124));
    _web_box->setBgOpa(235);
    _web_box->setBorderWidth(2);
    _web_box->setBorderColor(lv_color_hex(0x8B5CF6));
    _web_box->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _web_box->setHidden(true);

    _web_title = std::make_unique<Label>(_web_box->get());
    _web_title->setTextFont(&MontserratSemiBold26);
    _web_title->setTextColor(lv_color_hex(0xC084FC));
    _web_title->setText("Web Config");
    _web_title->align(LV_ALIGN_TOP_MID, 0, 16);

    _web_info = std::make_unique<Label>(_web_box->get());
    _web_info->setTextFont(&lv_font_misans_18);
    _web_info->setTextColor(lv_color_hex(0xF1F5F9));
    _web_info->setWidth(320);
    _web_info->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _web_info->setLongMode(LV_LABEL_LONG_MODE_WRAP);
    _web_info->setText("");
    _web_info->align(LV_ALIGN_CENTER, 0, 10);

    _web_hint = std::make_unique<Label>(_web_box->get());
    _web_hint->setTextFont(&lv_font_montserrat_14);
    _web_hint->setTextColor(lv_color_hex(0xA78BFA));
    _web_hint->setText("Tap screen or press Btn A to exit");
    _web_hint->align(LV_ALIGN_BOTTOM_MID, 0, -14);

    _pattern_touch->moveForeground();
}

void GeminiView::setState(gemini::SessionState state, bool isTalking, bool webConfigActive)
{
    _state             = state;
    _is_talking        = isTalking;
    _web_config_active = webConfigActive;

    if (_web_box) {
        _web_box->setHidden(!_web_config_active);
        if (_web_config_active) {
            _web_box->moveForeground();
        }
    }
}

void GeminiView::setStatusText(const std::string& status)
{
    if (_status_label) {
        _status_label->setText(status);
    }
}

void GeminiView::setTranscriptText(const std::string& userText, const std::string& modelText)
{
    if (_web_config_active && _web_info) {
        _web_info->setText(modelText.empty() ? userText : modelText);
        return;
    }

    if (!_transcript_label) {
        return;
    }

    if (!modelText.empty()) {
        _transcript_label->setTextColor(lv_color_hex(0xF8FAFC));
        _transcript_label->setText(modelText);
    } else if (!userText.empty()) {
        _transcript_label->setTextColor(lv_color_hex(0x7DD3FC));
        _transcript_label->setText(userText);
    }
}

void GeminiView::setAudioLevel(float level)
{
    _audio_level = std::clamp(level, 0.0f, 1.0f);
}

void GeminiView::setSpectrum(const SpectrumBands& bands)
{
    _target_bands = bands;
}

void GeminiView::setPeakFrequencyHz(float frequencyHz)
{
    _peak_frequency_hz = std::max(0.0f, frequencyHz);
}

void GeminiView::updateReducedBands()
{
    constexpr int per_band = static_cast<int>(band_count / reduced_band_count);
    for (std::size_t band = 0; band < reduced_band_count; ++band) {
        std::size_t start  = band * per_band;
        std::size_t end    = start + per_band;
        float weighted_sum = 0.0f;
        float weight_sum   = 0.0f;

        for (std::size_t i = start; i < end; ++i) {
            float emphasis = 1.0f - static_cast<float>(i - start) * 0.12f;
            weighted_sum += _display_bands[i] * emphasis;
            weight_sum += emphasis;
        }

        _reduced_bands[band] = weight_sum > 0.0f ? (weighted_sum / weight_sum) : 0.0f;
    }
}

void GeminiView::update()
{
    _anim_tick++;

    // Audio level smoothing (fast attack, smooth release)
    float level_alpha = (_audio_level > _smooth_level) ? 0.55f : 0.20f;
    _smooth_level += (_audio_level - _smooth_level) * level_alpha;

    // Pitch smoothing (normalize human voice range: 80Hz - 1000Hz)
    float target_pitch = std::clamp((_peak_frequency_hz - 80.0f) / 920.0f, 0.0f, 1.0f);
    _smooth_pitch += (target_pitch - _smooth_pitch) * 0.32f;

    // Smooth spectrum bands from HAL audio FFT
    for (std::size_t i = 0; i < band_count; ++i) {
        float target = std::clamp(_target_bands[i], 0.0f, 1.0f);
        float current = _display_bands[i];
        float alpha = target > current ? 0.65f : 0.35f;
        _display_bands[i] = current + (target - current) * alpha;
    }

    updateReducedBands();

    // Invalidate canvas to render the next frame of the living mist pattern
    if (_mist_canvas) {
        lv_obj_invalidate(_mist_canvas->get());
    }
}

}  // namespace view

namespace {

void mist_draw_event_cb(lv_event_t* e)
{
    auto* view = static_cast<GeminiView*>(lv_event_get_user_data(e));
    if (view == nullptr) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        lv_event_set_ext_draw_size(e, 0);
        return;
    }

    if (code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(e, LV_COVER_RES_COVER);
        return;
    }

    if (code != LV_EVENT_DRAW_MAIN_BEGIN) {
        return;
    }

    lv_obj_t* obj     = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_opa_t obj_opa  = lv_obj_get_style_opa_recursive(obj, LV_PART_MAIN);
    if (obj_opa <= LV_OPA_MIN) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_point_precise_t center = {
        static_cast<lv_value_precise_t>(coords.x1 + lv_obj_get_width(obj) / 2),
        static_cast<lv_value_precise_t>(coords.y1 + lv_obj_get_height(obj) / 2),
    };

    const float level  = view->smoothLevel();
    const float pitch  = view->smoothPitch();
    const auto& bands  = view->reducedBands();
    const float bass   = bands[0];
    const float low_m  = bands[1];
    const float high_m = bands[2];
    const float treble = bands[3];
    const uint32_t tick = view->animTick();

    const bool is_talking  = view->isTalking() || view->isPatternPressed();
    const bool web_active  = view->isWebConfigActive();
    const auto state       = view->state();

    uint32_t col_halo = 0x071526;
    uint32_t col_core = 0x7DD3FC;

    if (web_active) {
        col_halo = 0x1B0E2E;
        col_core = 0xE9D5FF;
    } else if (state == gemini::SessionState::Error) {
        col_halo = 0x260C0C;
        col_core = 0xFCA5A5;
    } else if (is_talking || state == gemini::SessionState::Listening) {
        col_halo = 0x061E28;
        col_core = 0xBAE6FD;
    } else if (state == gemini::SessionState::Thinking) {
        col_halo = 0x160B29;
        col_core = 0xE0E7FF;
    } else if (state == gemini::SessionState::Speaking) {
        col_halo = 0x0C1938;
        col_core = 0xFEF08A;
    }

    int breath = get_sin(static_cast<int32_t>(tick * 4), 5);
    int base_rot_speed = 2;
    if (state == gemini::SessionState::Thinking) {
        base_rot_speed = 6;
    } else if (state == gemini::SessionState::Speaking) {
        base_rot_speed = 3 + static_cast<int>(pitch * 3.0f);
    }
    int32_t rot = norm_deg(static_cast<int32_t>(tick * base_rot_speed));

    int base_r = 58 + static_cast<int>(level * 28.0f + bass * 14.0f) + breath;
    base_r = std::clamp(base_r, 48, 88);

    std::array<int, _num_sectors> r_halo;
    std::array<int, _num_sectors> r_orb;
    std::array<int, _num_sectors> r_core;

    for (int i = 0; i < _num_sectors; ++i) {
        int32_t deg = i * _deg_per_sector;

        int32_t h1 = get_cos(2 * deg + rot, 5 + static_cast<int>(bass * 14.0f));
        int32_t h2 = get_sin(3 * deg - rot * 2, 4 + static_cast<int>(low_m * 10.0f));
        int32_t h3 = get_cos(4 * deg + rot * 3, 3 + static_cast<int>(high_m * 8.0f + pitch * 6.0f));
        int32_t h4 = get_sin(6 * deg - rot, 2 + static_cast<int>(treble * 6.0f));

        int rad = base_r + (h1 + h2 + h3 + h4);
        rad = std::clamp(rad, 40, 92);

        r_orb[i]  = rad;
        r_halo[i] = std::min(rad + 12, 106);
        r_core[i] = std::max(static_cast<int>(rad * 0.52f), 24) + static_cast<int>(level * 8.0f);
    }

    lv_draw_triangle_dsc_t draw_dsc;
    lv_draw_triangle_dsc_init(&draw_dsc);
    draw_dsc.opa = LV_OPA_COVER;

    // Layer 1: Ambient deep cosmic halo
    draw_dsc.color = lv_color_hex(col_halo);
    for (int i = 0; i < _num_sectors; ++i) {
        int next_i = (i + 1) % _num_sectors;
        int32_t deg1 = i * _deg_per_sector;
        int32_t deg2 = next_i * _deg_per_sector;

        draw_dsc.p[0] = center;
        draw_dsc.p[1].x = center.x + get_cos(deg1, r_halo[i]);
        draw_dsc.p[1].y = center.y + get_sin(deg1, r_halo[i]);
        draw_dsc.p[2].x = center.x + get_cos(deg2, r_halo[next_i]);
        draw_dsc.p[2].y = center.y + get_sin(deg2, r_halo[next_i]);
        lv_draw_triangle(layer, &draw_dsc);
    }

    // Layer 2: Main energetic reactive orb
    for (int i = 0; i < _num_sectors; ++i) {
        int next_i = (i + 1) % _num_sectors;
        int32_t deg1 = i * _deg_per_sector;
        int32_t deg2 = next_i * _deg_per_sector;

        draw_dsc.color = get_sector_color(deg1, rot, state, is_talking, web_active, pitch);
        draw_dsc.p[0] = center;
        draw_dsc.p[1].x = center.x + get_cos(deg1, r_orb[i]);
        draw_dsc.p[1].y = center.y + get_sin(deg1, r_orb[i]);
        draw_dsc.p[2].x = center.x + get_cos(deg2, r_orb[next_i]);
        draw_dsc.p[2].y = center.y + get_sin(deg2, r_orb[next_i]);
        lv_draw_triangle(layer, &draw_dsc);
    }

    // Layer 3: Luminous inner core
    draw_dsc.color = lv_color_hex(col_core);
    for (int i = 0; i < _num_sectors; ++i) {
        int next_i = (i + 1) % _num_sectors;
        int32_t deg1 = i * _deg_per_sector;
        int32_t deg2 = next_i * _deg_per_sector;

        draw_dsc.p[0] = center;
        draw_dsc.p[1].x = center.x + get_cos(deg1, r_core[i]);
        draw_dsc.p[1].y = center.y + get_sin(deg1, r_core[i]);
        draw_dsc.p[2].x = center.x + get_cos(deg2, r_core[next_i]);
        draw_dsc.p[2].y = center.y + get_sin(deg2, r_core[next_i]);
        lv_draw_triangle(layer, &draw_dsc);
    }

    // Layer 4: Orbiting satellite sparks
    int r_sat = std::clamp(base_r + 14, 55, 102);
    draw_dsc.color = lv_color_hex(0xFFFFFF);
    for (int s = 0; s < 4; ++s) {
        int32_t s_deg = norm_deg(rot + s * 90);
        float sx = center.x + get_cos(s_deg, r_sat);
        float sy = center.y + get_sin(s_deg, r_sat);
        const float spark_size = 3.0f;

        draw_dsc.p[0].x = sx;
        draw_dsc.p[0].y = sy - spark_size;
        draw_dsc.p[1].x = sx - spark_size;
        draw_dsc.p[1].y = sy;
        draw_dsc.p[2].x = sx + spark_size;
        draw_dsc.p[2].y = sy;
        lv_draw_triangle(layer, &draw_dsc);

        draw_dsc.p[0].x = sx;
        draw_dsc.p[0].y = sy + spark_size;
        lv_draw_triangle(layer, &draw_dsc);
    }

    // Layer 5: Center iconic Gemini 4-pointed sparkle
    int spark_len = 14 + static_cast<int>(level * 16.0f) + (breath >> 1);
    spark_len = std::clamp(spark_len, 12, 34);
    const float spark_thick = 4.0f;
    draw_dsc.color = lv_color_hex(0xFFFFFF);

    // Top spike
    draw_dsc.p[0].x = center.x;
    draw_dsc.p[0].y = center.y - spark_len;
    draw_dsc.p[1].x = center.x - spark_thick;
    draw_dsc.p[1].y = center.y;
    draw_dsc.p[2].x = center.x + spark_thick;
    draw_dsc.p[2].y = center.y;
    lv_draw_triangle(layer, &draw_dsc);

    // Bottom spike
    draw_dsc.p[0].x = center.x;
    draw_dsc.p[0].y = center.y + spark_len;
    lv_draw_triangle(layer, &draw_dsc);

    // Left spike
    draw_dsc.p[0].x = center.x - spark_len;
    draw_dsc.p[0].y = center.y;
    draw_dsc.p[1].x = center.x;
    draw_dsc.p[1].y = center.y - spark_thick;
    draw_dsc.p[2].x = center.x;
    draw_dsc.p[2].y = center.y + spark_thick;
    lv_draw_triangle(layer, &draw_dsc);

    // Right spike
    draw_dsc.p[0].x = center.x + spark_len;
    draw_dsc.p[0].y = center.y;
    lv_draw_triangle(layer, &draw_dsc);
}

}  // namespace

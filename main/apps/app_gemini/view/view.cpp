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

    // Dynamic living mist canvas covering the entire viewport
    _mist_canvas = std::make_unique<Container>(_panel->get());
    _mist_canvas->align(LV_ALIGN_CENTER, 0, 0);
    _mist_canvas->setSize(_panel_size, _panel_size);
    _mist_canvas->setRadius(0);
    _mist_canvas->setBorderWidth(0);
    _mist_canvas->setPaddingAll(0);
    _mist_canvas->setBgOpa(LV_OPA_TRANSP);
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
        lv_event_set_ext_draw_size(e, 180);
        return;
    }

    if (code == LV_EVENT_COVER_CHECK) {
        lv_event_set_cover_res(e, LV_COVER_RES_NOT_COVER);
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

    // Determine color theme based on conversational state
    uint32_t col_outer = 0x0C2340;
    uint32_t col_mid_1 = 0x0284C7;
    uint32_t col_mid_2 = 0x0EA5E9;
    uint32_t col_inner = 0x38BDF8;
    uint32_t col_core  = 0xBAE6FD;

    if (web_active) {
        col_outer = 0x4C1D95;
        col_mid_1 = 0x7C3AED;
        col_mid_2 = 0xA855F7;
        col_inner = 0xC084FC;
        col_core  = 0xEDE9FE;
    } else if (is_talking || state == gemini::SessionState::Listening) {
        // High-energy electric cyan & radiant white mist for voice input
        col_outer = 0x0E7490;
        col_mid_1 = 0x06B6D4;
        col_mid_2 = 0x22D3EE;
        col_inner = 0x67E8F9;
        col_core  = 0xFFFFFF;
    } else if (state == gemini::SessionState::Speaking) {
        // Luminous aurora shifting with AI voice pitch and harmonic energy
        uint8_t pitch_mix = static_cast<uint8_t>(std::clamp(pitch * 255.0f, 0.0f, 255.0f));
        col_outer = 0x1E3A8A;
        col_mid_1 = 0x3B82F6;
        col_mid_2 = 0xF59E0B;
        col_inner = lv_color_to_int(lv_color_mix(lv_color_hex(0x67E8F9), lv_color_hex(0xFCD34D), pitch_mix));
        col_core  = 0xFFFFF5;
    } else if (state == gemini::SessionState::Thinking) {
        // Mystic deep violet & purple nebula swirl
        col_outer = 0x3B0764;
        col_mid_1 = 0x6366F1;
        col_mid_2 = 0x9333EA;
        col_inner = 0xA855F7;
        col_core  = 0xF5F3FF;
    } else if (state == gemini::SessionState::Error) {
        // Soft rose / coral mist
        col_outer = 0x7F1D1D;
        col_mid_1 = 0xDC2626;
        col_mid_2 = 0xEF4444;
        col_inner = 0xF87171;
        col_core  = 0xFEE2E2;
    }

    // Organic idle breath and harmonic rotation speed
    int breath = get_sin(static_cast<int32_t>(tick * 3), 7) + 7;
    int base_rot_speed = 2 + static_cast<int>(pitch * 3.5f);
    if (state == gemini::SessionState::Thinking) {
        base_rot_speed = 6;
    }
    int32_t rot = norm_deg(static_cast<int32_t>(tick * base_rot_speed));

    int base_r = 82 + static_cast<int>(level * 52.0f + bass * 26.0f) + breath;

    // Radii buffers for the multi-layered mist cloud
    std::array<int, _num_sectors> r_mid;
    std::array<int, _num_sectors> r_out;
    std::array<int, _num_sectors> r_in;

    for (int i = 0; i < _num_sectors; ++i) {
        int32_t deg = i * _deg_per_sector;

        // Wave 1: 2 lobes driven by bass
        int32_t h1 = get_cos(2 * deg + rot, 8 + static_cast<int>(bass * 26.0f));
        // Wave 2: 3 lobes driven by voice fundamental / low-mid
        int32_t h2 = get_sin(3 * deg - rot * 2, 6 + static_cast<int>(low_m * 20.0f));
        // Wave 3: 5 lobes driven by speech formants / high-mid and pitch
        int32_t h3 = get_cos(5 * deg + rot * 3, 4 + static_cast<int>(high_m * 14.0f + pitch * 10.0f));
        // Wave 4: 7 lobes driven by treble fine mist shimmer
        int32_t h4 = get_sin(7 * deg - rot, 2 + static_cast<int>(treble * 10.0f));

        int rad = base_r + (h1 + h2 + h3 + h4);
        r_mid[i] = std::max(rad, 40);
        r_out[i] = static_cast<int>(r_mid[i] * 1.34f + 16);
        r_in[i]  = static_cast<int>(r_mid[i] * 0.70f);
    }

    lv_draw_triangle_dsc_t draw_dsc;
    lv_draw_triangle_dsc_init(&draw_dsc);
    draw_dsc.opa = LV_OPA_COVER;

    // -------------------------------------------------------------
    // Layer 1: Outer diffuse mist halo (pre-blended with cosmic background for 60 FPS opaque speed)
    // -------------------------------------------------------------
    draw_dsc.color = lv_color_mix(lv_color_hex(col_outer), lv_color_hex(0x05070D), 85);

    for (int i = 0; i < _num_sectors; ++i) {
        int next_i = (i + 1) % _num_sectors;
        int32_t deg1 = i * _deg_per_sector;
        int32_t deg2 = next_i * _deg_per_sector;

        draw_dsc.p[0] = center;
        draw_dsc.p[1].x = center.x + get_cos(deg1, r_out[i]);
        draw_dsc.p[1].y = center.y + get_sin(deg1, r_out[i]);
        draw_dsc.p[2].x = center.x + get_cos(deg2, r_out[next_i]);
        draw_dsc.p[2].y = center.y + get_sin(deg2, r_out[next_i]);
        lv_draw_triangle(layer, &draw_dsc);
    }

    // -------------------------------------------------------------
    // Layer 2: Middle flowing mist nebula (fluid organic cloud)
    // -------------------------------------------------------------
    for (int i = 0; i < _num_sectors; ++i) {
        int next_i = (i + 1) % _num_sectors;
        int32_t deg1 = i * _deg_per_sector;
        int32_t deg2 = next_i * _deg_per_sector;

        uint8_t mix = static_cast<uint8_t>((get_cos(deg1 * 2 + rot, 127) + 128));
        draw_dsc.color = lv_color_mix(lv_color_hex(col_mid_2), lv_color_hex(col_mid_1), mix);

        draw_dsc.p[0] = center;
        draw_dsc.p[1].x = center.x + get_cos(deg1, r_mid[i]);
        draw_dsc.p[1].y = center.y + get_sin(deg1, r_mid[i]);
        draw_dsc.p[2].x = center.x + get_cos(deg2, r_mid[next_i]);
        draw_dsc.p[2].y = center.y + get_sin(deg2, r_mid[next_i]);
        lv_draw_triangle(layer, &draw_dsc);
    }

    // -------------------------------------------------------------
    // Layer 3: Inner swirling mist aura (luminous depth layer)
    // -------------------------------------------------------------
    draw_dsc.color = lv_color_hex(col_inner);
    const int32_t angle_offset = 25;

    for (int i = 0; i < _num_sectors; ++i) {
        int next_i = (i + 1) % _num_sectors;
        int32_t deg1 = i * _deg_per_sector + angle_offset;
        int32_t deg2 = next_i * _deg_per_sector + angle_offset;

        draw_dsc.p[0] = center;
        draw_dsc.p[1].x = center.x + get_cos(deg1, r_in[i]);
        draw_dsc.p[1].y = center.y + get_sin(deg1, r_in[i]);
        draw_dsc.p[2].x = center.x + get_cos(deg2, r_in[next_i]);
        draw_dsc.p[2].y = center.y + get_sin(deg2, r_in[next_i]);
        lv_draw_triangle(layer, &draw_dsc);
    }

    // -------------------------------------------------------------
    // Layer 4: Center radiant core (pulsing heart of the orb)
    // -------------------------------------------------------------
    int core_r = 38 + static_cast<int>(level * 16.0f + bass * 12.0f);
    draw_dsc.color = lv_color_hex(col_core);

    for (int i = 0; i < _num_sectors; ++i) {
        int next_i = (i + 1) % _num_sectors;
        int32_t deg1 = i * _deg_per_sector;
        int32_t deg2 = next_i * _deg_per_sector;

        draw_dsc.p[0] = center;
        draw_dsc.p[1].x = center.x + get_cos(deg1, core_r);
        draw_dsc.p[1].y = center.y + get_sin(deg1, core_r);
        draw_dsc.p[2].x = center.x + get_cos(deg2, core_r);
        draw_dsc.p[2].y = center.y + get_sin(deg2, core_r);
        lv_draw_triangle(layer, &draw_dsc);
    }

    // -------------------------------------------------------------
    // Layer 5: Center Gemini four-pointed sparkle
    // -------------------------------------------------------------
    int spark_len   = 16 + static_cast<int>(level * 12.0f + breath * 0.4f);
    const float spark_thick = 4.0f;
    draw_dsc.color  = lv_color_hex(0xFFFFFF);

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

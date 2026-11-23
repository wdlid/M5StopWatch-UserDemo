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

void orbs_draw_event_cb(lv_event_t* e);

constexpr int _panel_size = 466;
constexpr int _orb_count  = 4;

struct OrbPalette {
    uint32_t halo;
    uint32_t body;
    uint32_t highlight;
    uint32_t core;
};

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
    deg            = norm_deg(deg);
    int32_t result = lv_trigo_cos(deg) * amplitude;
    result += LV_TRIGO_SIN_MAX / 2;
    return result >> LV_TRIGO_SHIFT;
}

int32_t get_sin(int32_t deg, int32_t amplitude)
{
    deg            = norm_deg(deg);
    int32_t result = lv_trigo_sin(deg) * amplitude;
    return (result + LV_TRIGO_SIN_MAX / 2) >> LV_TRIGO_SHIFT;
}

void draw_circle(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t radius, uint32_t color_hex, lv_opa_t opa)
{
    if (radius <= 0 || opa <= LV_OPA_MIN) {
        return;
    }
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius       = LV_RADIUS_CIRCLE;
    dsc.bg_color     = lv_color_hex(color_hex);
    dsc.bg_opa       = opa;
    dsc.border_width = 0;

    lv_area_t area = {
        cx - radius,
        cy - radius,
        cx + radius,
        cy + radius,
    };
    lv_draw_rect(layer, &dsc, &area);
}

void draw_orb(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t radius, uint32_t bg_hex, uint32_t halo_hex,
              uint32_t body_hex, uint32_t highlight_hex, uint8_t halo_mix, bool is_light)
{
    if (radius <= 1) {
        return;
    }

    // Pre-blend outer halo against theme background for fast opaque rendering
    int32_t halo_r    = static_cast<int32_t>(radius * (is_light ? 1.48f : 1.65f));
    uint32_t halo_col = lv_color_to_int(lv_color_mix(lv_color_hex(halo_hex), lv_color_hex(bg_hex), halo_mix));
    draw_circle(layer, cx, cy, halo_r, halo_col, LV_OPA_COVER);

    // Mid soft aura
    int32_t mid_r    = static_cast<int32_t>(radius * 1.22f);
    uint8_t mid_mix  = static_cast<uint8_t>(std::min<int>(255, halo_mix + (is_light ? 75 : 65)));
    uint32_t mid_col = lv_color_to_int(lv_color_mix(lv_color_hex(body_hex), lv_color_hex(bg_hex), mid_mix));
    draw_circle(layer, cx, cy, mid_r, mid_col, LV_OPA_COVER);

    // Main orb body
    draw_circle(layer, cx, cy, radius, body_hex, LV_OPA_COVER);

    // Specular highlight / inner glow
    int32_t spec_r   = std::max<int32_t>(2, static_cast<int32_t>(radius * 0.42f));
    int32_t spec_off = std::max<int32_t>(1, static_cast<int32_t>(radius * 0.24f));
    draw_circle(layer, cx - spec_off, cy - spec_off, spec_r, highlight_hex, LV_OPA_COVER);
}

OrbPalette select_palette(gemini::SessionState state, bool is_talking, bool web_active, float pitch, bool is_light)
{
    if (web_active) {
        return is_light ? OrbPalette{0xA78BFA, 0x7C3AED, 0xEDE9FE, 0x6D28D9}
                        : OrbPalette{0x6D28D9, 0x8B5CF6, 0xEDE9FE, 0xA855F7};
    }

    if (is_talking || state == gemini::SessionState::Listening) {
        return is_light ? OrbPalette{0x14B8A6, 0x0F766E, 0xCCFBF1, 0x0D9488}
                        : OrbPalette{0x0891B2, 0x22D3EE, 0xFFFFFF, 0x67E8F9};
    }

    if (state == gemini::SessionState::Speaking) {
        uint8_t pitch_mix = static_cast<uint8_t>(std::clamp(pitch * 255.0f, 0.0f, 255.0f));
        if (is_light) {
            uint32_t body = lv_color_to_int(lv_color_mix(lv_color_hex(0xD97757), lv_color_hex(0x2563EB), pitch_mix));
            uint32_t halo = lv_color_to_int(lv_color_mix(lv_color_hex(0xF59E0B), lv_color_hex(0x3B82F6), pitch_mix));
            return {halo, body, 0xFFFBEB, 0xD97757};
        }
        uint32_t body = lv_color_to_int(lv_color_mix(lv_color_hex(0xF59E0B), lv_color_hex(0x3B82F6), pitch_mix));
        uint32_t hl   = lv_color_to_int(lv_color_mix(lv_color_hex(0xFDE68A), lv_color_hex(0xBAE6FD), pitch_mix));
        return {0x1E40AF, body, hl, 0xFFFFF5};
    }

    if (state == gemini::SessionState::Thinking) {
        return is_light ? OrbPalette{0x9333EA, 0x6D28D9, 0xF3E8FF, 0x7C3AED}
                        : OrbPalette{0x6366F1, 0xA855F7, 0xF5F3FF, 0xC084FC};
    }

    if (state == gemini::SessionState::Connecting) {
        return is_light ? OrbPalette{0x0EA5E9, 0x0369A1, 0xE0F2FE, 0x0284C7}
                        : OrbPalette{0x0284C7, 0x38BDF8, 0xE0F2FE, 0x7DD3FC};
    }

    if (state == gemini::SessionState::Error) {
        return is_light ? OrbPalette{0xEF4444, 0xB91C1C, 0xFEE2E2, 0xDC2626}
                        : OrbPalette{0x991B1B, 0xEF4444, 0xFEE2E2, 0xF87171};
    }

    if (state == gemini::SessionState::Disconnected) {
        return is_light ? OrbPalette{0xB8AFA2, 0x8A8175, 0xEFE9DF, 0x9E9587}
                        : OrbPalette{0x334155, 0x475569, 0x94A3B8, 0x64748B};
    }

    // SessionState::Ready (idle)
    return is_light ? OrbPalette{0x3B82F6, 0x1D4ED8, 0xEFF6FF, 0x2563EB}
                    : OrbPalette{0x1D4ED8, 0x60A5FA, 0xDBEAFE, 0x93C5FD};
}

}  // namespace

namespace view {

void GeminiView::init(lv_obj_t* parent)
{
    // Fullscreen container (466x466 circular screen)
    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(_panel_size, _panel_size);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    // Dynamic Thinking Orbs canvas covering the entire viewport
    _orbs_canvas = std::make_unique<Container>(_panel->get());
    _orbs_canvas->align(LV_ALIGN_CENTER, 0, 0);
    _orbs_canvas->setSize(_panel_size, _panel_size);
    _orbs_canvas->setRadius(0);
    _orbs_canvas->setBorderWidth(0);
    _orbs_canvas->setPaddingAll(0);
    _orbs_canvas->setBgOpa(LV_OPA_TRANSP);
    _orbs_canvas->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _orbs_canvas->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(_orbs_canvas->get(), orbs_draw_event_cb, LV_EVENT_ALL, this);
    lv_obj_refresh_ext_draw_size(_orbs_canvas->get());

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
    _status_label->setWidth(380);
    _status_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _status_label->setText("Connecting...");
    _status_label->align(LV_ALIGN_TOP_MID, 0, 36);

    // Subtitle transcript at bottom (no buttons!)
    _transcript_label = std::make_unique<Label>(_panel->get());
    _transcript_label->setTextFont(&lv_font_misans_18);
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
    _web_box->setBgOpa(240);
    _web_box->setBorderWidth(2);
    _web_box->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _web_box->setHidden(true);

    _web_title = std::make_unique<Label>(_web_box->get());
    _web_title->setTextFont(&MontserratSemiBold26);
    _web_title->setText("Web Config");
    _web_title->align(LV_ALIGN_TOP_MID, 0, 16);

    _web_info = std::make_unique<Label>(_web_box->get());
    _web_info->setTextFont(&lv_font_misans_18);
    _web_info->setWidth(320);
    _web_info->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _web_info->setLongMode(LV_LABEL_LONG_MODE_WRAP);
    _web_info->setText("");
    _web_info->align(LV_ALIGN_CENTER, 0, 10);

    _web_hint = std::make_unique<Label>(_web_box->get());
    _web_hint->setTextFont(&lv_font_montserrat_14);
    _web_hint->setText("Tap screen or press Btn A to exit");
    _web_hint->align(LV_ALIGN_BOTTOM_MID, 0, -14);

    _pattern_touch->moveForeground();
    applyTheme();
}

void GeminiView::applyTheme()
{
    const auto mode    = GetHAL().getThemeMode();
    const auto& theme  = GetHAL().getTheme();
    _last_theme        = mode;

    if (_panel) {
        _panel->setBgColor(lv_color_hex(theme.background));
    }
    if (_status_label) {
        _status_label->setTextColor(lv_color_hex(theme.textSecondary));
    }
    if (_transcript_label) {
        _transcript_label->setTextColor(lv_color_hex(_is_user_transcript ? theme.accent : theme.textPrimary));
    }
    if (_web_box) {
        _web_box->setBgColor(lv_color_hex(theme.cardBackground));
        _web_box->setBorderColor(lv_color_hex(theme.border));
    }
    if (_web_title) {
        _web_title->setTextColor(lv_color_hex(theme.accent));
    }
    if (_web_info) {
        _web_info->setTextColor(lv_color_hex(theme.textPrimary));
    }
    if (_web_hint) {
        _web_hint->setTextColor(lv_color_hex(theme.textSecondary));
    }
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

    const auto& theme = GetHAL().getTheme();
    if (!modelText.empty()) {
        _is_user_transcript = false;
        _transcript_label->setTextColor(lv_color_hex(theme.textPrimary));
        _transcript_label->setText(modelText);
    } else if (!userText.empty()) {
        _is_user_transcript = true;
        _transcript_label->setTextColor(lv_color_hex(theme.accent));
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

    if (GetHAL().getThemeMode() != _last_theme) {
        applyTheme();
    }

    // Audio level smoothing (fast attack, smooth release)
    float level_alpha = (_audio_level > _smooth_level) ? 0.55f : 0.20f;
    _smooth_level += (_audio_level - _smooth_level) * level_alpha;

    // Pitch smoothing (normalize human voice range: 80Hz - 1000Hz)
    float target_pitch = std::clamp((_peak_frequency_hz - 80.0f) / 920.0f, 0.0f, 1.0f);
    _smooth_pitch += (target_pitch - _smooth_pitch) * 0.32f;

    // Smooth spectrum bands from HAL audio FFT
    for (std::size_t i = 0; i < band_count; ++i) {
        float target      = std::clamp(_target_bands[i], 0.0f, 1.0f);
        float current     = _display_bands[i];
        float alpha       = target > current ? 0.65f : 0.35f;
        _display_bands[i] = current + (target - current) * alpha;
    }

    updateReducedBands();

    // Smoothly accumulate orbital phase and energy per conversational state
    float phase_step    = 1.4f;
    float target_energy = 0.35f;
    const bool talking  = _is_talking || _pattern_pressed;

    if (_web_config_active) {
        phase_step    = 0.8f;
        target_energy = 0.22f;
    } else if (talking || _state == gemini::SessionState::Listening) {
        phase_step    = 3.0f + _smooth_level * 2.8f;
        target_energy = 0.65f + _smooth_level * 0.35f;
    } else if (_state == gemini::SessionState::Speaking) {
        phase_step    = 2.6f + _smooth_pitch * 3.0f;
        target_energy = 0.60f + _smooth_level * 0.40f;
    } else if (_state == gemini::SessionState::Thinking) {
        phase_step    = 5.4f;
        target_energy = 0.85f;
    } else if (_state == gemini::SessionState::Connecting) {
        phase_step    = 3.8f;
        target_energy = 0.50f;
    } else if (_state == gemini::SessionState::Error) {
        phase_step    = 1.2f;
        target_energy = 0.45f;
    } else if (_state == gemini::SessionState::Disconnected) {
        phase_step    = 0.6f;
        target_energy = 0.15f;
    }

    _orbit_phase = std::fmod(_orbit_phase + phase_step, 360.0f);
    _smooth_energy += (target_energy - _smooth_energy) * 0.15f;

    if (_orbs_canvas) {
        lv_obj_invalidate(_orbs_canvas->get());
    }
}

}  // namespace view

namespace {

void orbs_draw_event_cb(lv_event_t* e)
{
    auto* view = static_cast<GeminiView*>(lv_event_get_user_data(e));
    if (view == nullptr) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        lv_event_set_ext_draw_size(e, 120);
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

    const int32_t cx = coords.x1 + lv_obj_get_width(obj) / 2;
    const int32_t cy = coords.y1 + lv_obj_get_height(obj) / 2;

    const float level      = view->smoothLevel();
    const float pitch      = view->smoothPitch();
    const float energy     = view->smoothEnergy();
    const auto& bands      = view->reducedBands();
    const uint32_t tick    = view->animTick();
    const int32_t rot_deg  = static_cast<int32_t>(view->orbitPhase());
    const bool is_talking  = view->isTalking() || view->isPatternPressed();
    const bool web_active  = view->isWebConfigActive();
    const auto state       = view->state();
    const bool is_light    = (GetHAL().getThemeMode() == Hal::ThemeMode::Light);
    const uint32_t bg_hex  = GetHAL().getTheme().background;
    const OrbPalette pal   = select_palette(state, is_talking, web_active, pitch, is_light);

    // Per-orb subtle hue variation so the constellation feels alive
    constexpr std::array<uint32_t, _orb_count> _dark_accents  = {0x38BDF8, 0x818CF8, 0xC084FC, 0x2DD4BF};
    constexpr std::array<uint32_t, _orb_count> _light_accents = {0x0284C7, 0x4F46E5, 0x9333EA, 0x0D9488};

    // Base orbital radius & orb size driven by state + energy
    int32_t orbit_r   = 46 + static_cast<int32_t>(energy * 26.0f);
    int32_t base_size = 16 + static_cast<int32_t>(energy * 8.0f);
    uint8_t halo_mix  = is_light ? 65 : 85;

    if (web_active) {
        orbit_r   = 82;
        base_size = 14;
        halo_mix  = is_light ? 45 : 60;
    } else if (state == gemini::SessionState::Disconnected) {
        orbit_r   = 34;
        base_size = 12;
        halo_mix  = is_light ? 40 : 50;
    } else if (state == gemini::SessionState::Error) {
        orbit_r   = 44 + get_sin(static_cast<int32_t>(tick * 12), 4);
        base_size = 18;
    }

    // Center anchor orb (pulses gently with voice/thinking)
    int32_t core_pulse = get_sin(static_cast<int32_t>(tick * 4), 3);
    int32_t core_r     = 14 + static_cast<int32_t>(level * 14.0f + energy * 6.0f) + core_pulse;
    if (state == gemini::SessionState::Disconnected || web_active) {
        core_r = 10;
    }
    draw_orb(layer, cx, cy, core_r, bg_hex, pal.halo, pal.core, pal.highlight, halo_mix, is_light);

    // 4 orbiting Thinking Orbs
    for (int i = 0; i < _orb_count; ++i) {
        const int32_t phase_offset = i * 90;
        const float band           = bands[static_cast<std::size_t>(i)];

        int32_t angle_deg = rot_deg + phase_offset;
        int32_t r_orb     = orbit_r;
        int32_t orb_r     = base_size;

        if (!web_active && (is_talking || state == gemini::SessionState::Listening)) {
            // Listening: orbs expand/contract with each frequency band + gentle wobble
            r_orb += static_cast<int32_t>(band * 34.0f + level * 18.0f);
            orb_r += static_cast<int32_t>(band * 12.0f + level * 6.0f);
        } else if (!web_active && state == gemini::SessionState::Speaking) {
            // Speaking: harmonic radial dance driven by audio spectrum + pitch
            int32_t harmonic = get_sin(static_cast<int32_t>(tick * 6) + phase_offset, 10);
            r_orb += static_cast<int32_t>(band * 36.0f + level * 16.0f) + harmonic;
            orb_r += static_cast<int32_t>(band * 14.0f + level * 8.0f);
        } else if (!web_active && state == gemini::SessionState::Thinking) {
            // Thinking: classic figure/wave phase-shifted orb choreography
            int32_t wave_r   = get_sin(static_cast<int32_t>(tick * 7) + phase_offset, 18);
            int32_t wave_sz  = get_cos(static_cast<int32_t>(tick * 7) + phase_offset, 6);
            int32_t tang_wob = get_sin(static_cast<int32_t>(tick * 4) + phase_offset * 2, 12);
            angle_deg += tang_wob;
            r_orb += wave_r;
            orb_r += wave_sz;
        } else if (!web_active && state == gemini::SessionState::Connecting) {
            // Connecting: sequential chasing pulse around the ring
            int32_t chase = get_sin(static_cast<int32_t>(tick * 8) - phase_offset, 8);
            orb_r += std::max<int32_t>(0, chase);
        } else if (!web_active && state == gemini::SessionState::Ready) {
            // Ready: calm breathing constellation
            int32_t breath_r  = get_sin(static_cast<int32_t>(tick * 3) + phase_offset, 6);
            int32_t breath_sz = get_cos(static_cast<int32_t>(tick * 3) + phase_offset, 3);
            r_orb += breath_r;
            orb_r += breath_sz;
        } else if (!web_active && state == gemini::SessionState::Error) {
            // Error: subtle horizontal/angular jitter
            int32_t jitter = get_sin(static_cast<int32_t>(tick * 18) + phase_offset * 3, 6);
            r_orb += jitter;
        }

        const int32_t ox = cx + get_cos(angle_deg, r_orb);
        const int32_t oy = cy + get_sin(angle_deg, r_orb);

        // Blend per-orb accent subtly into the state palette for Thinking & Ready
        uint32_t orb_body = pal.body;
        uint32_t orb_halo = pal.halo;
        if (!web_active && (state == gemini::SessionState::Thinking || state == gemini::SessionState::Ready)) {
            uint32_t accent = is_light ? _light_accents[static_cast<std::size_t>(i)]
                                       : _dark_accents[static_cast<std::size_t>(i)];
            orb_body = lv_color_to_int(lv_color_mix(lv_color_hex(accent), lv_color_hex(pal.body), 115));
            orb_halo = lv_color_to_int(lv_color_mix(lv_color_hex(accent), lv_color_hex(pal.halo), 115));
        }

        draw_orb(layer, ox, oy, std::max<int32_t>(6, orb_r), bg_hex, orb_halo, orb_body, pal.highlight, halo_mix,
                 is_light);
    }
}

}  // namespace

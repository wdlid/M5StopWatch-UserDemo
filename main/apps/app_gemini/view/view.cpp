/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif

namespace {

using namespace view;
using namespace uitk::lvgl_cpp;

constexpr int _panel_size        = 466;
constexpr int _canvas_size       = GeminiView::orb_canvas_size;
constexpr int _dot_count         = 380;
constexpr float _pi              = 3.1415926535f;
constexpr float _two_pi          = 6.283185307f;
constexpr float _golden_angle    = 2.3999632297f;        // pi * (3 - sqrt(5))
constexpr float _tilt_rad        = 20.0f * _pi / 180.0f; // Official ThinkingOrbs 20 deg tilt
constexpr float _sphere_radius   = 148.0f;
constexpr float _dot_base_radius = 3.83f;
constexpr float _record_period_s = 1.35f;                // Slow pulsing cycle for `using, record`

// Official ThinkingOrbs `working` ring axis: tipped 30 deg toward viewer, rolled 10 deg
constexpr float _ring_axis_x = -0.15038373f;  // -sin(10 deg) * cos(30 deg)
constexpr float _ring_axis_y = 0.85286853f;   //  cos(10 deg) * cos(30 deg)
constexpr float _ring_axis_z = 0.50000000f;   //  sin(30 deg)

// Official ThinkingOrbs `searching-lighthouse` beam lean (30 deg) and trail (90 deg)
constexpr float _beam_lean_sin = 0.50000000f;
constexpr float _beam_lean_cos = 0.86602540f;
constexpr float _beam_trail    = 0.5f * _pi;

struct SpherePoint {
    float x;
    float y;
    float z;
};

void* alloc_canvas_mem(std::size_t bytes)
{
#ifdef ESP_PLATFORM
    void* ptr = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != nullptr) {
        return ptr;
    }
#endif
    return std::malloc(bytes);
}

void free_canvas_mem(void* ptr)
{
    if (ptr == nullptr) {
        return;
    }
#ifdef ESP_PLATFORM
    heap_caps_free(ptr);
#else
    std::free(ptr);
#endif
}

uint64_t get_time_us()
{
#ifdef ESP_PLATFORM
    return static_cast<uint64_t>(esp_timer_get_time());
#else
    return static_cast<uint64_t>(lv_tick_get()) * 1000ULL;
#endif
}

// Evenly distributed unit-sphere Fibonacci lattice matching official ThinkingOrbs `distribute()`
const std::vector<SpherePoint>& get_sphere_points()
{
    static const auto pts = []() {
        std::vector<SpherePoint> out;
        out.reserve(_dot_count);
        for (int i = 0; i < _dot_count; ++i) {
            const float y  = 1.0f - (2.0f * (static_cast<float>(i) + 0.5f)) / static_cast<float>(_dot_count);
            const float r  = std::sqrt(std::max(0.0f, 1.0f - y * y));
            const float th = static_cast<float>(i) * _golden_angle;
            out.push_back({r * std::cos(th), y, r * std::sin(th)});
        }
        return out;
    }();
    return pts;
}

uint16_t pack_rgb565(uint32_t r8, uint32_t g8, uint32_t b8, bool force_achromatic)
{
    const uint16_t r5 = static_cast<uint16_t>((r8 * 31U + 127U) / 255U);
    if (force_achromatic) {
        // Matched 5-bit/6-bit greyscale avoids green/magenta sub-steps on RGB565 AMOLED panels
        const uint16_t g6 = static_cast<uint16_t>((r5 << 1) | (r5 >> 4));
        return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | r5);
    }
    const uint16_t g6 = static_cast<uint16_t>((g8 * 63U + 127U) / 255U);
    const uint16_t b5 = static_cast<uint16_t>((b8 * 31U + 127U) / 255U);
    return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

void build_color_lut(std::array<uint16_t, 256>& lut, uint32_t bg_hex, gemini::SessionState state, bool is_light)
{
    // Official ThinkingOrbs monochrome silver/white ink (no Gemini blue); soft red only on connection error
    uint32_t ink_hex = is_light ? 0x646B78 : 0xFAFAFA;
    if (state == gemini::SessionState::Error) {
        ink_hex = is_light ? 0xDC2626 : 0xF87171;
    }

    const uint32_t bg_r  = (bg_hex >> 16) & 0xFFU;
    const uint32_t bg_g  = (bg_hex >> 8) & 0xFFU;
    const uint32_t bg_b  = bg_hex & 0xFFU;
    const uint32_t ink_r = (ink_hex >> 16) & 0xFFU;
    const uint32_t ink_g = (ink_hex >> 8) & 0xFFU;
    const uint32_t ink_b = ink_hex & 0xFFU;

    const bool achromatic = (bg_r == bg_g && bg_g == bg_b && ink_r == ink_g && ink_g == ink_b);

    for (uint32_t a = 0; a < 256U; ++a) {
        const uint32_t inv = 255U - a;
        const uint32_t r8  = (ink_r * a + bg_r * inv + 127U) / 255U;
        const uint32_t g8  = (ink_g * a + bg_g * inv + 127U) / 255U;
        const uint32_t b8  = (ink_b * a + bg_b * inv + 127U) / 255U;
        lut[a]             = pack_rgb565(r8, g8, b8, achromatic);
    }
}

// Subpixel anti-aliased circle splatting in floating-point coordinates (eliminates pixel-grid jitter)
inline void splat_subpixel_dot(uint8_t* ink_buf, float fx, float fy, float fr, float fa)
{
    if (fa <= 0.004f || fr <= 0.25f) {
        return;
    }

    constexpr float aa_half = 0.60f;
    const float r_out       = fr + aa_half;
    const float r_in        = std::max(0.0f, fr - aa_half);
    const float r_out2      = r_out * r_out;
    const float r_in2       = r_in * r_in;
    const float inv_span    = 1.0f / (r_out - r_in);

    const int x0 = std::max(0, static_cast<int>(std::floor(fx - r_out)));
    const int x1 = std::min(_canvas_size - 1, static_cast<int>(std::ceil(fx + r_out)));
    const int y0 = std::max(0, static_cast<int>(std::floor(fy - r_out)));
    const int y1 = std::min(_canvas_size - 1, static_cast<int>(std::ceil(fy + r_out)));

    for (int py = y0; py <= y1; ++py) {
        const float dy  = (static_cast<float>(py) + 0.5f) - fy;
        const float dy2 = dy * dy;
        uint8_t* row    = ink_buf + py * _canvas_size;

        for (int px = x0; px <= x1; ++px) {
            const float dx = (static_cast<float>(px) + 0.5f) - fx;
            const float d2 = dx * dx + dy2;
            if (d2 >= r_out2) {
                continue;
            }

            float cov = 1.0f;
            if (d2 > r_in2) {
                const float t = (r_out - std::sqrt(d2)) * inv_span;
                cov           = t * t * (3.0f - 2.0f * t);
            }

            const uint32_t a_px = static_cast<uint32_t>(fa * cov * 255.0f + 0.5f);
            if (a_px == 0U) {
                continue;
            }

            // Order-independent Porter-Duff source-over for identical ink color
            const uint32_t prev = row[px];
            const uint32_t prod = (255U - prev) * a_px + 128U;
            row[px]             = static_cast<uint8_t>(prev + ((prod + (prod >> 8)) >> 8));
        }
    }
}

}  // namespace

namespace view {

GeminiView::~GeminiView()
{
    if (_anim_timer) {
        lv_timer_delete(_anim_timer);
        _anim_timer = nullptr;
    }

    if (_orbs_canvas) {
        lv_obj_delete(_orbs_canvas);
        _orbs_canvas = nullptr;
    }

    _web_hint.reset();
    _web_info.reset();
    _web_title.reset();
    _web_box.reset();
    _transcript_label.reset();
    _status_label.reset();
    _pattern_touch.reset();
    _panel.reset();

    free_canvas_mem(_canvas_pixels);
    _canvas_pixels = nullptr;
    free_canvas_mem(_ink_alpha);
    _ink_alpha = nullptr;
}

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

    // Subpixel anti-aliased RGB565 orb canvas centered in the viewport
    const std::size_t pixel_count = static_cast<std::size_t>(_canvas_size) * static_cast<std::size_t>(_canvas_size);
    _canvas_pixels                = static_cast<uint16_t*>(alloc_canvas_mem(pixel_count * sizeof(uint16_t)));
    _ink_alpha                    = static_cast<uint8_t*>(alloc_canvas_mem(pixel_count * sizeof(uint8_t)));
    if (_ink_alpha) {
        std::memset(_ink_alpha, 0, pixel_count);
    }

    if (_canvas_pixels && _ink_alpha) {
        _orbs_canvas = lv_canvas_create(_panel->get());
        lv_canvas_set_buffer(_orbs_canvas, _canvas_pixels, _canvas_size, _canvas_size, LV_COLOR_FORMAT_RGB565);
        lv_obj_align(_orbs_canvas, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(_orbs_canvas, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(_orbs_canvas, LV_OBJ_FLAG_CLICKABLE);
    }

    // Center interactive touch target for pattern hold-to-talk
    _pattern_touch = std::make_unique<Container>(_panel->get());
    _pattern_touch->align(LV_ALIGN_CENTER, 0, 0);
    _pattern_touch->setSize(360, 360);
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
    _transcript_label->setText("");
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
    renderOrbFrame();

    _last_time_us = get_time_us();
    _anim_timer   = lv_timer_create(
        [](lv_timer_t* timer) {
            auto* view = static_cast<GeminiView*>(lv_timer_get_user_data(timer));
            if (view) {
                view->stepAnimation();
            }
        },
        15, this);
}

void GeminiView::applyTheme()
{
    const auto mode   = GetHAL().getThemeMode();
    const auto& theme = GetHAL().getTheme();
    _last_theme       = mode;

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

    if (_status_label) {
        _status_label->setHidden(_web_config_active);
    }
    if (_transcript_label) {
        _transcript_label->setHidden(_web_config_active);
    }
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
    } else {
        _transcript_label->setText("");
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

void GeminiView::stepAnimation()
{
    const uint64_t now_us = get_time_us();
    const uint64_t gap_us = (now_us > _last_time_us) ? (now_us - _last_time_us) : 15000ULL;
    _last_time_us         = now_us;
    const float dt        = std::clamp(static_cast<float>(gap_us) * 1e-6f, 0.001f, 0.05f);

    _anim_tick++;
    _anim_time_sec += dt;

    if (GetHAL().getThemeMode() != _last_theme) {
        applyTheme();
    }

    float level_rate = (_audio_level > _smooth_level) ? 14.0f : 6.0f;
    _smooth_level += (_audio_level - _smooth_level) * (1.0f - std::exp(-level_rate * dt));

    float target_pitch = std::clamp((_peak_frequency_hz - 80.0f) / 920.0f, 0.0f, 1.0f);
    _smooth_pitch += (target_pitch - _smooth_pitch) * (1.0f - std::exp(-10.0f * dt));

    const bool recording = !_web_config_active && (_is_talking || _pattern_pressed || _state == gemini::SessionState::Listening);
    if (recording) {
        if (!_was_recording) {
            // Start `using, record` pulse at peak (rec = 1.0) so entry from idle is completely seamless
            _record_phase = 0.0f;
        } else {
            _record_phase = std::fmod(_record_phase + dt / _record_period_s, 1.0f);
        }
        _record_mix += (1.0f - _record_mix) * (1.0f - std::exp(-10.0f * dt));
    } else {
        if (_record_mix > 0.002f) {
            _record_phase = std::fmod(_record_phase + dt / _record_period_s, 1.0f);
            _record_mix += (0.0f - _record_mix) * (1.0f - std::exp(-8.0f * dt));
        } else {
            _record_mix = 0.0f;
        }
    }
    _was_recording = recording;

    // Rotation speeds matched to official ThinkingOrbs `PERIOD` table
    float rot_speed     = _two_pi / 6.5f;  // `idle`: 6.5s per full turn
    float target_energy = 0.35f;

    if (_web_config_active) {
        rot_speed     = _two_pi / 13.0f;
        target_energy = 0.20f;
    } else if (recording) {
        rot_speed     = _two_pi / 13.0f;  // `using-record`: 13.0s per full turn
        target_energy = 0.70f;
    } else if (_state == gemini::SessionState::Speaking) {
        rot_speed     = _two_pi / 6.5f;
        target_energy = 0.65f + _smooth_level * 0.35f;
    } else if (_state == gemini::SessionState::Thinking) {
        rot_speed     = _two_pi / 3.6f;   // `working`: brisk steady turn
        target_energy = 0.72f;
    } else if (_state == gemini::SessionState::Connecting) {
        rot_speed     = _two_pi / 10.0f;  // `searching-lighthouse`
        target_energy = 0.50f;
    } else if (_state == gemini::SessionState::Error) {
        rot_speed     = _two_pi / 13.0f;
        target_energy = 0.45f;
    } else if (_state == gemini::SessionState::Disconnected) {
        rot_speed     = _two_pi / 13.0f;
        target_energy = 0.14f;
    }

    _orbit_phase = std::fmod(_orbit_phase + rot_speed * dt, _two_pi);
    _smooth_energy += (target_energy - _smooth_energy) * (1.0f - std::exp(-6.0f * dt));

    renderOrbFrame();
    if (_orbs_canvas) {
        lv_obj_invalidate(_orbs_canvas);
    }
}

void GeminiView::renderOrbFrame()
{
    if (!_canvas_pixels || !_ink_alpha) {
        return;
    }

    const bool is_light   = (GetHAL().getThemeMode() == Hal::ThemeMode::Light);
    const uint32_t bg_hex = GetHAL().getTheme().background;

    std::array<uint16_t, 256> lut{};
    build_color_lut(lut, bg_hex, _state, is_light);

    const float center = 0.5f * static_cast<float>(_canvas_size);
    const float cos_y  = std::cos(_orbit_phase);
    const float sin_y  = std::sin(_orbit_phase);
    const float cos_t  = std::cos(_tilt_rad);
    const float sin_t  = std::sin(_tilt_rad);

    // Precompute `using, record` slow pulse (0 = dim & smaller, 1 = full size & bright)
    const float rec_tri   = std::abs(1.0f - 2.0f * _record_phase);
    const float rec_cos   = 0.5f * (1.0f + std::cos(_two_pi * _record_phase));
    const float rec_val   = 0.5f * rec_tri + 0.5f * rec_cos;
    const float rec_scale = 1.0f - _record_mix * (1.0f - (0.88f + 0.12f * rec_val));
    const float rec_opa   = 1.0f - _record_mix * (1.0f - (0.32f + 0.68f * rec_val));

    // Precompute `working` sweeping light ring for Thinking state
    const bool is_thinking = !_web_config_active && (_record_mix < 0.5f) && (_state == gemini::SessionState::Thinking);
    float work_at          = 0.0f;
    bool work_ring_on      = false;
    if (is_thinking) {
        const float u_ms   = std::fmod(_anim_time_sec * 1000.0f, 1700.0f);
        work_ring_on       = (u_ms < 1200.0f);
        const float ease_u = 0.5f * (1.0f - std::cos(_pi * std::min(1.0f, u_ms / 1200.0f)));
        work_at            = 1.3f - 2.6f * ease_u;
    }

    // Precompute `searching-lighthouse` beam angle for Connecting state
    const bool is_connecting =
        !_web_config_active && (_record_mix < 0.5f) && (_state == gemini::SessionState::Connecting);
    const float beam_phase = std::fmod(_anim_time_sec / 2.5f, 1.0f) * _two_pi - _pi;

    const bool is_speaking = !_web_config_active && (_record_mix < 0.05f) && (_state == gemini::SessionState::Speaking);
    const float speak_scale = is_speaking ? (1.0f + _smooth_level * 0.07f) : 1.0f;

    const auto& pts = get_sphere_points();
    for (const auto& p : pts) {
        // Exact ThinkingOrbs orthographic rotation: yaw around Y, fixed 20 deg tilt around X
        const float z1 = -p.x * sin_y + p.z * cos_y;
        float vx       = p.x * cos_y + p.z * sin_y;
        float vy       = p.y * cos_t - z1 * sin_t;
        const float vz = p.y * sin_t + z1 * cos_t;

        const float d = 0.5f * (vz + 1.0f);
        if (d <= 0.30f) {
            continue;
        }

        // Exact ThinkingOrbs depth-graded dot size and smooth horizon fade
        float dot_r = (0.5f + 1.4f * d) * _dot_base_radius;
        float dot_a = (d - 0.30f) / 0.70f;

        if (_web_config_active) {
            dot_a *= 0.35f;
        } else if (_record_mix > 0.001f) {
            // `using, record`: whole sphere slowly breathes in size and intensity
            vx *= rec_scale;
            vy *= rec_scale;
            dot_r *= (0.92f + 0.08f * rec_scale);
            dot_a *= rec_opa;
        } else if (is_thinking) {
            // `working`: diagonal light ring sweeping smoothly across the round sphere
            dot_a *= 0.52f;
            if (work_ring_on) {
                const float q    = vx * _ring_axis_x + vy * _ring_axis_y + vz * _ring_axis_z;
                const float diff = (q - work_at) / 0.20f;
                const float g    = std::exp(-(diff * diff));
                dot_r *= (1.0f + 0.45f * g);
                dot_a += (1.0f - dot_a) * g;
            }
        } else if (is_connecting) {
            // `searching-lighthouse`: sweeping beam across the silver sphere
            dot_a *= 0.45f;
            const float across = vx * _beam_lean_cos - vy * _beam_lean_sin;
            const float toward = -sin_t * (vx * _beam_lean_sin + vy * _beam_lean_cos) + cos_t * vz;
            const float off    = std::atan2(across, toward) - beam_phase;
            float dphi         = std::fmod(off + _pi, _two_pi);
            if (dphi < 0.0f) {
                dphi += _two_pi;
            }
            dphi -= _pi;
            const float beam =
                (dphi < 0.0f) ? std::max(0.0f, 1.0f + dphi / _beam_trail) : std::exp(-((dphi / 0.45f) * (dphi / 0.45f)));
            const float near_w = std::clamp((d - 0.40f) / 0.30f, 0.0f, 1.0f);
            const float g      = beam * (0.25f + 0.75f * near_w);
            dot_r *= (1.0f + 0.45f * g);
            dot_a += (1.0f - dot_a) * g;
        } else if (is_speaking) {
            // Uniform spherical voice pulse without latitude-wave flattening
            vx *= speak_scale;
            vy *= speak_scale;
            const float voice_boost = std::min(1.0f, _smooth_level * 1.4f);
            dot_r *= (1.0f + 0.12f * voice_boost * d);
            dot_a *= (0.80f + 0.20f * voice_boost);
        } else if (_state == gemini::SessionState::Error) {
            const float pulse = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(_anim_time_sec * 3.0f));
            dot_a *= pulse;
        } else if (_state == gemini::SessionState::Disconnected) {
            dot_a *= 0.45f;
        }
        // SessionState::Ready (idle): pure rigid sphere, zero radial wave deformation

        const float fx = center + vx * _sphere_radius;
        const float fy = center - vy * _sphere_radius;
        splat_subpixel_dot(_ink_alpha, fx, fy, dot_r, std::clamp(dot_a, 0.0f, 1.0f));
    }

    // Single-pass LUT resolve into RGB565 canvas + zeroing ink buffer for next frame
    const std::size_t total_px = static_cast<std::size_t>(_canvas_size) * static_cast<std::size_t>(_canvas_size);
    const uint16_t bg565       = lut[0];
    for (std::size_t i = 0; i < total_px; ++i) {
        const uint8_t a = _ink_alpha[i];
        if (a == 0U) {
            _canvas_pixels[i] = bg565;
        } else {
            _canvas_pixels[i] = lut[a];
            _ink_alpha[i]     = 0U;
        }
    }
}

void GeminiView::update()
{
    // Animation stepping & invalidation are driven by _anim_timer
}

}  // namespace view

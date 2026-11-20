/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include "gemini_lottie_assets.h"
#include <assets/assets.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <esp_heap_caps.h>

namespace {

using namespace view;
using namespace uitk::lvgl_cpp;

constexpr int _panel_size = 466;

}  // namespace

namespace view {

GeminiView::GeminiView() = default;

GeminiView::~GeminiView()
{
    if (_lottie_obj) {
        lv_obj_del(_lottie_obj);
        _lottie_obj = nullptr;
    }
    if (_lottie_buf) {
        free(_lottie_buf);
        _lottie_buf = nullptr;
    }
}

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

    // High performance Lottie vector animation for Google Assistant / Gemini
    static constexpr int _lottie_size       = 200;
    static constexpr size_t _lottie_buf_size = _lottie_size * _lottie_size * 4;
    _lottie_buf = static_cast<uint8_t*>(heap_caps_malloc(_lottie_buf_size, MALLOC_CAP_SPIRAM));
    if (_lottie_buf) {
        _lottie_obj = lv_lottie_create(_panel->get());
        lv_lottie_set_buffer(_lottie_obj, _lottie_size, _lottie_size, _lottie_buf);
        lv_obj_align(_lottie_obj, LV_ALIGN_CENTER, 0, -10);
        lv_obj_remove_flag(_lottie_obj, LV_OBJ_FLAG_CLICKABLE);
        lv_lottie_set_src_data(_lottie_obj, lottie_gemini_listening, lottie_gemini_listening_size);
        _active_lottie_mode = 0;
    }

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

    if (_lottie_obj) {
        int target_mode = 0;  // 0: listening/idle, 1: thinking, 2: speaking
        if (state == gemini::SessionState::Thinking) {
            target_mode = 1;
        } else if (state == gemini::SessionState::Speaking) {
            target_mode = 2;
        } else {
            target_mode = 0;  // Listening / Ready / Connected / Disconnected
        }

        if (target_mode != _active_lottie_mode) {
            _active_lottie_mode = target_mode;
            if (_active_lottie_mode == 0) {
                lv_lottie_set_src_data(_lottie_obj, lottie_gemini_listening, lottie_gemini_listening_size);
            } else if (_active_lottie_mode == 1) {
                lv_lottie_set_src_data(_lottie_obj, lottie_gemini_thinking, lottie_gemini_thinking_size);
            } else if (_active_lottie_mode == 2) {
                lv_lottie_set_src_data(_lottie_obj, lottie_gemini_speaking, lottie_gemini_speaking_size);
            }
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

    // Voice-energy audio dynamic animation modulation
    if (_lottie_obj && (_is_talking || _state == gemini::SessionState::Speaking)) {
        lv_anim_t* anim = lv_lottie_get_anim(_lottie_obj);
        if (anim) {
            int32_t base_duration = (_active_lottie_mode == 2) ? 1200 : 2000;
            int32_t active_duration =
                base_duration - static_cast<int32_t>(_smooth_level * (base_duration * 0.40f));
            lv_anim_set_duration(anim, std::max<int32_t>(active_duration, 600));
        }
    }
}

}  // namespace view

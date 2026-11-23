/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../gemini_client.h"
#include <hal/hal.h>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>

namespace view {

class GeminiView {
public:
    static constexpr std::size_t band_count         = 20;
    static constexpr std::size_t reduced_band_count = 4;
    using SpectrumBands                             = std::array<float, band_count>;
    using ReducedSpectrumBands                      = std::array<float, reduced_band_count>;

    std::function<void(bool)> onPatternPressedChanged;
    std::function<void()> onPatternClicked;
    std::function<void()> onExitWebConfigClicked;

    void init(lv_obj_t* parent);
    void setState(gemini::SessionState state, bool isTalking, bool webConfigActive);
    void setStatusText(const std::string& status);
    void setTranscriptText(const std::string& userText, const std::string& modelText);
    void setAudioLevel(float level);
    void setSpectrum(const SpectrumBands& bands);
    void setPeakFrequencyHz(float frequencyHz);
    void update();

    bool isPatternPressed() const
    {
        return _pattern_pressed;
    }

    const ReducedSpectrumBands& reducedBands() const
    {
        return _reduced_bands;
    }
    float smoothLevel() const
    {
        return _smooth_level;
    }
    float smoothPitch() const
    {
        return _smooth_pitch;
    }
    gemini::SessionState state() const
    {
        return _state;
    }
    bool isTalking() const
    {
        return _is_talking;
    }
    bool isWebConfigActive() const
    {
        return _web_config_active;
    }
    uint32_t animTick() const
    {
        return _anim_tick;
    }
    float orbitPhase() const
    {
        return _orbit_phase;
    }
    float smoothEnergy() const
    {
        return _smooth_energy;
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Container> _orbs_canvas;
    std::unique_ptr<uitk::lvgl_cpp::Container> _pattern_touch;
    std::unique_ptr<uitk::lvgl_cpp::Label> _status_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _transcript_label;
    std::unique_ptr<uitk::lvgl_cpp::Container> _web_box;
    std::unique_ptr<uitk::lvgl_cpp::Label> _web_title;
    std::unique_ptr<uitk::lvgl_cpp::Label> _web_info;
    std::unique_ptr<uitk::lvgl_cpp::Label> _web_hint;

    gemini::SessionState _state   = gemini::SessionState::Disconnected;
    Hal::ThemeMode _last_theme    = Hal::ThemeMode::Dark;
    bool _is_talking              = false;
    bool _web_config_active       = false;
    bool _pattern_pressed         = false;
    bool _is_user_transcript      = false;

    float _audio_level       = 0.0f;
    float _smooth_level      = 0.0f;
    float _peak_frequency_hz = 0.0f;
    float _smooth_pitch      = 0.0f;
    float _orbit_phase       = 0.0f;
    float _smooth_energy     = 0.2f;
    uint32_t _anim_tick      = 0;

    SpectrumBands _target_bands         = {};
    SpectrumBands _display_bands        = {};
    ReducedSpectrumBands _reduced_bands = {};

    void applyTheme();
    void updateReducedBands();
};

}  // namespace view

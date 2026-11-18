/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "../gemini_client.h"
#include <functional>
#include <memory>
#include <string>
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>

namespace view {

class GeminiView {
public:
    std::function<void()> onTalkButtonClicked;
    std::function<void(bool)> onTalkButtonPressedChanged;
    std::function<void()> onConfigButtonClicked;

    void init(lv_obj_t* parent);
    void setState(gemini::SessionState state, bool isTalking, bool webConfigActive);
    void setStatusText(const std::string& status);
    void setTranscriptText(const std::string& userText, const std::string& modelText);
    void setAudioLevel(float level);
    void update();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _title_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _status_label;
    std::unique_ptr<uitk::lvgl_cpp::Container> _orb_outer;
    std::unique_ptr<uitk::lvgl_cpp::Container> _orb_mid;
    std::unique_ptr<uitk::lvgl_cpp::Container> _orb_core;
    std::unique_ptr<uitk::lvgl_cpp::Label> _orb_label;
    std::unique_ptr<uitk::lvgl_cpp::Label> _transcript_label;
    std::unique_ptr<uitk::lvgl_cpp::Button> _talk_btn;
    std::unique_ptr<uitk::lvgl_cpp::Button> _cfg_btn;

    gemini::SessionState _state = gemini::SessionState::Disconnected;
    bool _is_talking            = false;
    bool _web_config_active     = false;
    float _audio_level          = 0.0f;
    float _smooth_level         = 0.0f;
    uint32_t _anim_tick         = 0;
};

}  // namespace view

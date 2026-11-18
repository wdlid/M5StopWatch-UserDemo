/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <algorithm>
#include <cmath>

using namespace view;
using namespace uitk::lvgl_cpp;

void GeminiView::init(lv_obj_t* parent)
{
    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(466, 466);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgColor(lv_color_hex(0x090D16));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _title_label = std::make_unique<Label>(_panel->get());
    _title_label->setTextFont(&MontserratSemiBold26);
    _title_label->setTextColor(lv_color_hex(0x7DD3FC));
    _title_label->setText("Gemini 3.8 Live");
    _title_label->align(LV_ALIGN_TOP_MID, 0, 28);

    _status_label = std::make_unique<Label>(_panel->get());
    _status_label->setTextFont(&lv_font_montserrat_16);
    _status_label->setTextColor(lv_color_hex(0x94A3B8));
    _status_label->setWidth(340);
    _status_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _status_label->setText("Connecting...");
    _status_label->align(LV_ALIGN_TOP_MID, 0, 62);

    // Animated outer halo
    _orb_outer = std::make_unique<Container>(_panel->get());
    _orb_outer->setSize(196, 196);
    _orb_outer->setRadius(LV_RADIUS_CIRCLE);
    _orb_outer->setBorderWidth(2);
    _orb_outer->setBorderColor(lv_color_hex(0x38BDF8));
    _orb_outer->setBgColor(lv_color_hex(0x0284C7));
    _orb_outer->setBgOpa(35);
    _orb_outer->align(LV_ALIGN_CENTER, 0, -48);
    _orb_outer->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _orb_outer->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    _orb_mid = std::make_unique<Container>(_panel->get());
    _orb_mid->setSize(152, 152);
    _orb_mid->setRadius(LV_RADIUS_CIRCLE);
    _orb_mid->setBorderWidth(0);
    _orb_mid->setBgColor(lv_color_hex(0x38BDF8));
    _orb_mid->setBgOpa(75);
    _orb_mid->align(LV_ALIGN_CENTER, 0, -48);
    _orb_mid->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _orb_mid->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    _orb_core = std::make_unique<Container>(_panel->get());
    _orb_core->setSize(112, 112);
    _orb_core->setRadius(LV_RADIUS_CIRCLE);
    _orb_core->setBorderWidth(2);
    _orb_core->setBorderColor(lv_color_hex(0xE0F2FE));
    _orb_core->setBgColor(lv_color_hex(0x0EA5E9));
    _orb_core->setBgOpa(235);
    _orb_core->align(LV_ALIGN_CENTER, 0, -48);
    _orb_core->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _orb_core->onClick().connect([this]() {
        if (onTalkButtonClicked) {
            onTalkButtonClicked();
        }
    });

    _orb_label = std::make_unique<Label>(_orb_core->get());
    _orb_label->setTextFont(&lv_font_montserrat_22);
    _orb_label->setTextColor(lv_color_hex(0xFFFFFF));
    _orb_label->setText("AI");
    _orb_label->align(LV_ALIGN_CENTER, 0, 0);

    // Subtitle / transcript area
    _transcript_label = std::make_unique<Label>(_panel->get());
    _transcript_label->setTextFont(&lv_font_montserrat_18);
    _transcript_label->setTextColor(lv_color_hex(0xE2E8F0));
    _transcript_label->setWidth(370);
    _transcript_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _transcript_label->setLongMode(LV_LABEL_LONG_MODE_WRAP);
    _transcript_label->setText("Press BtnA or tap button below to talk");
    _transcript_label->align(LV_ALIGN_CENTER, 0, 74);

    // Bottom Left/Center: Talk Button
    _talk_btn = std::make_unique<Button>(_panel->get());
    _talk_btn->setSize(224, 68);
    _talk_btn->setRadius(34);
    _talk_btn->setBorderWidth(0);
    _talk_btn->setShadowWidth(0);
    _talk_btn->setBgColor(lv_color_hex(0x4AD78C));
    _talk_btn->align(LV_ALIGN_BOTTOM_MID, -48, -36);
    _talk_btn->label().setText("Tap to Talk");
    _talk_btn->label().setTextFont(&lv_font_montserrat_22);
    _talk_btn->label().setTextColor(lv_color_hex(0x06371D));
    _talk_btn->label().align(LV_ALIGN_CENTER, 0, 0);
    _talk_btn->onClick().connect([this]() {
        if (onTalkButtonClicked) {
            onTalkButtonClicked();
        }
    });

    // Bottom Right: Web Config AP Button
    _cfg_btn = std::make_unique<Button>(_panel->get());
    _cfg_btn->setSize(86, 68);
    _cfg_btn->setRadius(34);
    _cfg_btn->setBorderWidth(0);
    _cfg_btn->setShadowWidth(0);
    _cfg_btn->setBgColor(lv_color_hex(0x263147));
    _cfg_btn->align(LV_ALIGN_BOTTOM_MID, 118, -36);
    _cfg_btn->label().setText("Web");
    _cfg_btn->label().setTextFont(&lv_font_montserrat_20);
    _cfg_btn->label().setTextColor(lv_color_hex(0x93C5FD));
    _cfg_btn->label().align(LV_ALIGN_CENTER, 0, 0);
    _cfg_btn->onClick().connect([this]() {
        if (onConfigButtonClicked) {
            onConfigButtonClicked();
        }
    });
}

void GeminiView::setState(gemini::SessionState state, bool isTalking, bool webConfigActive)
{
    _state             = state;
    _is_talking        = isTalking;
    _web_config_active = webConfigActive;
}

void GeminiView::setStatusText(const std::string& status)
{
    if (_status_label) {
        _status_label->setText(status);
    }
}

void GeminiView::setTranscriptText(const std::string& userText, const std::string& modelText)
{
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

void GeminiView::update()
{
    _anim_tick++;
    float alpha   = (_audio_level > _smooth_level) ? 0.48f : 0.20f;
    _smooth_level = _smooth_level + (_audio_level - _smooth_level) * alpha;

    float phase  = static_cast<float>(_anim_tick) * 0.08f;
    float breath = (std::sin(phase) + 1.0f) * 0.5f;

    uint32_t core_color  = 0x0EA5E9;
    uint32_t mid_color   = 0x38BDF8;
    uint32_t outer_color = 0x0284C7;
    const char* orb_txt  = "AI";

    if (_web_config_active) {
        core_color  = 0x8B5CF6;
        mid_color   = 0xA78BFA;
        outer_color = 0x6D28D9;
        orb_txt     = "CFG";
    } else if (_is_talking || _state == gemini::SessionState::Listening) {
        core_color  = 0x06B6D4;
        mid_color   = 0x22D3EE;
        outer_color = 0x0891B2;
        orb_txt     = "MIC";
    } else if (_state == gemini::SessionState::Speaking) {
        core_color  = 0xF59E0B;
        mid_color   = 0xFBBF24;
        outer_color = 0xD97706;
        orb_txt     = "LIVE";
    } else if (_state == gemini::SessionState::Thinking) {
        core_color  = 0x6366F1;
        mid_color   = 0x818CF8;
        outer_color = 0x4F46E5;
        orb_txt     = "...";
    } else if (_state == gemini::SessionState::Error) {
        core_color  = 0xEF4444;
        mid_color   = 0xF87171;
        outer_color = 0xB91C1C;
        orb_txt     = "ERR";
    } else if (_state == gemini::SessionState::Ready) {
        core_color  = 0x10B981;
        mid_color   = 0x34D399;
        outer_color = 0x059669;
        orb_txt     = "AI";
    }

    int dynamic_boost = static_cast<int>(_smooth_level * 48.0f + breath * 14.0f);
    int outer_sz      = 176 + dynamic_boost;
    int mid_sz        = 140 + (dynamic_boost * 2) / 3;

    if (_orb_outer && _orb_mid && _orb_core && _orb_label) {
        _orb_outer->setSize(outer_sz, outer_sz);
        _orb_outer->setBgColor(lv_color_hex(outer_color));
        _orb_outer->setBorderColor(lv_color_hex(mid_color));
        _orb_outer->align(LV_ALIGN_CENTER, 0, -48);

        _orb_mid->setSize(mid_sz, mid_sz);
        _orb_mid->setBgColor(lv_color_hex(mid_color));
        _orb_mid->align(LV_ALIGN_CENTER, 0, -48);

        _orb_core->setBgColor(lv_color_hex(core_color));
        _orb_label->setText(orb_txt);
    }

    if (_talk_btn) {
        if (_is_talking) {
            _talk_btn->setBgColor(lv_color_hex(0xEF4444));
            _talk_btn->label().setTextColor(lv_color_hex(0xFFFFFF));
            _talk_btn->label().setText("Stop & Send");
        } else {
            _talk_btn->setBgColor(lv_color_hex(0x4AD78C));
            _talk_btn->label().setTextColor(lv_color_hex(0x06371D));
            _talk_btn->label().setText("Tap to Talk");
        }
    }

    if (_cfg_btn) {
        if (_web_config_active) {
            _cfg_btn->setBgColor(lv_color_hex(0x8B5CF6));
            _cfg_btn->label().setTextColor(lv_color_hex(0xFFFFFF));
            _cfg_btn->label().setText("Exit");
        } else {
            _cfg_btn->setBgColor(lv_color_hex(0x263147));
            _cfg_btn->label().setTextColor(lv_color_hex(0x93C5FD));
            _cfg_btn->label().setText("Web");
        }
    }
}

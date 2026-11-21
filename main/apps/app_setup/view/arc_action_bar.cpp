/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "arc_action_bar.h"
#include <assets/assets.h>
#include <hal/hal.h>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

lv_obj_t* create_edge_arc_label(lv_obj_t* parent, float angleStart, float angleSize, uint32_t color)
{
    lv_obj_t* arc = lv_arclabel_create(parent);
    lv_obj_set_size(arc, 466, 466);
    lv_obj_center(arc);
    lv_arclabel_set_radius(arc, 212);
    lv_arclabel_set_angle_start(arc, angleStart);
    lv_arclabel_set_angle_size(arc, angleSize);
    lv_arclabel_set_dir(arc, LV_ARCLABEL_DIR_CLOCKWISE);
    lv_arclabel_set_text_horizontal_align(arc, LV_ARCLABEL_TEXT_ALIGN_CENTER);
    lv_arclabel_set_text_vertical_align(arc, LV_ARCLABEL_TEXT_ALIGN_CENTER);
    lv_arclabel_set_overflow(arc, LV_ARCLABEL_OVERFLOW_VISIBLE);
    lv_obj_set_style_text_font(arc, &lv_font_maple_mono_medium_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(arc, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
    return arc;
}

std::unique_ptr<Container> create_hit_area(lv_obj_t* parent, int x, int y, lv_event_cb_t cb, void* userData)
{
    auto hit = std::make_unique<Container>(parent);
    hit->setSize(124, 72);
    hit->align(LV_ALIGN_CENTER, x, y);
    hit->setBgOpa(LV_OPA_TRANSP);
    hit->setBorderWidth(0);
    hit->setShadowWidth(0);
    hit->setPaddingAll(0);
    hit->setRadius(36);
    hit->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    hit->addFlag(LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(hit->get(), lv_color_hex(GetHAL().getTheme().textPrimary), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(hit->get(), LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_add_event_cb(hit->get(), cb, LV_EVENT_CLICKED, userData);
    return hit;
}

}  // namespace

ArcActionBar::ArcActionBar(lv_obj_t* parent,
                           const char* leftText,
                           const char* rightText,
                           const char* titleText,
                           uint32_t leftColor,
                           uint32_t rightColor)
{
    _left_arc  = create_edge_arc_label(parent, 206.0f, 50.0f, leftColor);
    _right_arc = create_edge_arc_label(parent, 284.0f, 50.0f, rightColor);
    lv_obj_add_event_cb(
        _left_arc,
        [](lv_event_t* e) {
            auto** ptr = static_cast<lv_obj_t**>(lv_event_get_user_data(e));
            if (ptr) {
                *ptr = nullptr;
            }
        },
        LV_EVENT_DELETE, &_left_arc);
    lv_obj_add_event_cb(
        _right_arc,
        [](lv_event_t* e) {
            auto** ptr = static_cast<lv_obj_t**>(lv_event_get_user_data(e));
            if (ptr) {
                *ptr = nullptr;
            }
        },
        LV_EVENT_DELETE, &_right_arc);

    _left_hit = create_hit_area(
        parent, -108, -156,
        [](lv_event_t* e) {
            auto* self = static_cast<ArcActionBar*>(lv_event_get_user_data(e));
            if (self && !self->_left_text.empty()) {
                self->_left_clicked = true;
            }
        },
        this);

    _right_hit = create_hit_area(
        parent, 108, -156,
        [](lv_event_t* e) {
            auto* self = static_cast<ArcActionBar*>(lv_event_get_user_data(e));
            if (self && !self->_right_text.empty()) {
                self->_right_clicked = true;
            }
        },
        this);

    _title_label = std::make_unique<Label>(parent);
    _title_label->align(LV_ALIGN_TOP_MID, 0, 56);
    _title_label->setTextFont(&MontserratSemiBold26);
    _title_label->setTextColor(lv_color_hex(GetHAL().getTheme().textPrimary));
    _title_label->setWidth(240);
    _title_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _title_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    _title_label->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _title_label->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    setLeftText(leftText, leftColor);
    setRightText(rightText, rightColor);
    setTitle(titleText);
}

ArcActionBar::~ArcActionBar()
{
    if (_left_arc) {
        lv_obj_delete(_left_arc);
        _left_arc = nullptr;
    }
    if (_right_arc) {
        lv_obj_delete(_right_arc);
        _right_arc = nullptr;
    }
}

void ArcActionBar::update()
{
    if (!_left_text.empty() && GetHAL().btnA.wasClicked()) {
        _left_clicked = true;
    }
    if (!_right_text.empty() && GetHAL().btnB.wasClicked()) {
        _right_clicked = true;
    }
}

void ArcActionBar::setLeftText(const char* text, uint32_t color)
{
    _left_text = text ? text : "";
    if (_left_arc) {
        lv_arclabel_set_text(_left_arc, _left_text.c_str());
        lv_obj_set_style_text_color(_left_arc, lv_color_hex(color), LV_PART_MAIN);
        if (_left_text.empty()) {
            lv_obj_add_flag(_left_arc, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(_left_arc, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_left_hit) {
        _left_hit->setHidden(_left_text.empty());
    }
}

void ArcActionBar::setRightText(const char* text, uint32_t color)
{
    _right_text = text ? text : "";
    if (_right_arc) {
        lv_arclabel_set_text(_right_arc, _right_text.c_str());
        lv_obj_set_style_text_color(_right_arc, lv_color_hex(color), LV_PART_MAIN);
        if (_right_text.empty()) {
            lv_obj_add_flag(_right_arc, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(_right_arc, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_right_hit) {
        _right_hit->setHidden(_right_text.empty());
    }
}

void ArcActionBar::setTitle(const char* text)
{
    if (!_title_label) {
        return;
    }
    if (text && text[0] != '\0') {
        _title_label->setText(text);
        _title_label->setHidden(false);
    } else {
        _title_label->setText("");
        _title_label->setHidden(true);
    }
}

bool ArcActionBar::consumeLeftClicked()
{
    bool clicked  = _left_clicked;
    _left_clicked = false;
    return clicked;
}

bool ArcActionBar::consumeRightClicked()
{
    bool clicked   = _right_clicked;
    _right_clicked = false;
    return clicked;
}

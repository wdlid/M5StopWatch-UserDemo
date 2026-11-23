/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <mooncake.h>
#include <mooncake_log.h>
#include <cstdint>
#include <functional>
#include <smooth_ui_toolkit.hpp>
#include <smooth_lvgl.hpp>
#include <assets/assets.h>
#include <fmt/chrono.h>
#include <hal/hal.h>
#include <memory>
#include <vector>
#include <lvgl.h>

using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

/**
 * @brief Detects top-down swipe to open Quick Settings and upward swipe to close it.
 */
class StatuBarGesture {
public:
    std::function<void(void)> onGesture;
    std::function<void(void)> onSwipeUp;

    StatuBarGesture() : _is_tracking_down(false), _is_tracking_up(false), _last_state(LV_INDEV_STATE_REL)
    {
    }

    void init()
    {
        _is_tracking_down = false;
        _is_tracking_up   = false;
        _last_state       = LV_INDEV_STATE_REL;
        _top_threshold    = 60;  // Trigger within top 60px (status bar area)
        _swipe_min_dist   = 45;
    }

    void update(bool isDropdownOpen)
    {
        lv_indev_t* indev = GetHAL().lvTouchpad;
        if (!indev) {
            return;
        }

        lv_indev_state_t state = lv_indev_get_state(indev);
        lv_point_t curr_point;
        lv_indev_get_point(indev, &curr_point);

        if (state == LV_INDEV_STATE_PR && _last_state == LV_INDEV_STATE_REL) {
            if (!isDropdownOpen && curr_point.y <= _top_threshold && curr_point.y >= 0) {
                _start_point      = curr_point;
                _is_tracking_down = true;
                _is_tracking_up   = false;
            } else if (isDropdownOpen) {
                _start_point      = curr_point;
                _is_tracking_up   = true;
                _is_tracking_down = false;
            } else {
                _is_tracking_down = false;
                _is_tracking_up   = false;
            }
        } else if (state == LV_INDEV_STATE_REL && _last_state == LV_INDEV_STATE_PR) {
            int delta_y = curr_point.y - _start_point.y;
            int delta_x = abs(curr_point.x - _start_point.x);

            if (_is_tracking_down && delta_y > _swipe_min_dist && delta_y > delta_x) {
                if (onGesture) {
                    onGesture();
                }
            } else if (_is_tracking_up && (-delta_y) > _swipe_min_dist && (-delta_y) > delta_x) {
                if (onSwipeUp) {
                    onSwipeUp();
                }
            }
            _is_tracking_down = false;
            _is_tracking_up   = false;
        }

        _last_state = state;
    }

private:
    bool _is_tracking_down;
    bool _is_tracking_up;
    lv_indev_state_t _last_state;
    lv_point_t _start_point;
    int _top_threshold;
    int _swipe_min_dist;
};

namespace status_bar_view {

constexpr uint32_t _brightness_accent = 0x9C661F;  // Warm amber/ochre
constexpr uint32_t _volume_accent     = 0x1F6E58;  // Deep forest teal

class QuickSettingRow {
public:
    QuickSettingRow(lv_obj_t* parent, int y, const char* symbol, const char* title, int minVal, int maxVal,
                    uint32_t accentColor, std::function<void(int, bool)> onChanged)
        : _on_changed(std::move(onChanged))
    {
        auto& theme = GetHAL().getTheme();

        _box = std::make_unique<Container>(parent);
        _box->setSize(298, 80);
        _box->align(LV_ALIGN_TOP_MID, 0, y);
        _box->setRadius(16);
        _box->setBgColor(lv_color_hex(theme.background));
        _box->setBgOpa(LV_OPA_COVER);
        _box->setBorderWidth(1);
        _box->setBorderColor(lv_color_hex(theme.border));
        _box->setPaddingAll(0);
        _box->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _icon = std::make_unique<Label>(_box->get());
        _icon->setText(symbol);
        _icon->setTextFont(&lv_font_montserrat_16);
        _icon->setTextColor(lv_color_hex(accentColor));
        _icon->align(LV_ALIGN_TOP_LEFT, 14, 12);

        _title = std::make_unique<Label>(_box->get());
        _title->setText(title);
        _title->setTextFont(&lv_font_montserrat_16);
        _title->setTextColor(lv_color_hex(theme.textPrimary));
        _title->align(LV_ALIGN_TOP_LEFT, 40, 12);

        _value_label = std::make_unique<Label>(_box->get());
        _value_label->setText("0%");
        _value_label->setTextFont(&lv_font_montserrat_16);
        _value_label->setTextColor(lv_color_hex(accentColor));
        _value_label->align(LV_ALIGN_TOP_RIGHT, -14, 12);

        _slider = std::make_unique<Slider>(_box->get());
        _slider->setSize(266, 8);
        _slider->align(LV_ALIGN_BOTTOM_MID, 0, -16);
        _slider->setRange(minVal, maxVal, false);
        _slider->setBgColor(lv_color_hex(theme.border), LV_PART_MAIN);
        _slider->setBgOpa(LV_OPA_COVER, LV_PART_MAIN);
        _slider->setBorderWidth(0, LV_PART_MAIN);
        _slider->setRadius(LV_RADIUS_CIRCLE, LV_PART_MAIN);

        _slider->setBgColor(lv_color_hex(accentColor), LV_PART_INDICATOR);
        _slider->setBgOpa(LV_OPA_COVER, LV_PART_INDICATOR);
        _slider->setBorderWidth(0, LV_PART_INDICATOR);
        _slider->setRadius(LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

        lv_obj_set_style_bg_color(_slider->get(), lv_color_hex(theme.cardBackground), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(_slider->get(), LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_border_width(_slider->get(), 2, LV_PART_KNOB);
        lv_obj_set_style_border_color(_slider->get(), lv_color_hex(accentColor), LV_PART_KNOB);
        lv_obj_set_style_radius(_slider->get(), LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_set_style_pad_all(_slider->get(), 5, LV_PART_KNOB);

        _slider->onValueChanged().connect([this](int32_t val) {
            if (_syncing) {
                return;
            }
            updateLabel(static_cast<int>(val));
            if (_on_changed) {
                _on_changed(static_cast<int>(val), false);
            }
        });

        lv_obj_add_event_cb(
            _slider->get(),
            [](lv_event_t* e) {
                auto* self = static_cast<QuickSettingRow*>(lv_event_get_user_data(e));
                if (self && self->_on_changed) {
                    int val = lv_slider_get_value(self->_slider->get());
                    self->_on_changed(val, true);
                }
            },
            LV_EVENT_RELEASED, this);
    }

    void setValue(int val)
    {
        _syncing = true;
        _slider->setValue(val);
        _syncing = false;
        updateLabel(val);
    }

    bool isSliderPressed() const
    {
        return _slider && lv_obj_has_state(_slider->get(), LV_STATE_PRESSED);
    }

private:
    void updateLabel(int val)
    {
        _value_label->setText(fmt::format("{}%", val));
    }

    std::unique_ptr<Container> _box;
    std::unique_ptr<Label> _icon;
    std::unique_ptr<Label> _title;
    std::unique_ptr<Label> _value_label;
    std::unique_ptr<Slider> _slider;
    std::function<void(int, bool)> _on_changed;
    bool _syncing = false;
};

class StatusBarView {
public:
    StatusBarView(lv_obj_t* parent, uint32_t /*colorSecondary*/, uint32_t /*colorPrimary*/)
    {
        auto& theme = GetHAL().getTheme();
        bool is_light = (GetHAL().getThemeMode() == Hal::ThemeMode::Light);

        // Full-screen dimmed backdrop
        _backdrop = std::make_unique<Container>(parent);
        _backdrop->setSize(466, 466);
        _backdrop->align(LV_ALIGN_CENTER, 0, 0);
        _backdrop->setRadius(0);
        _backdrop->setBorderWidth(0);
        _backdrop->setPaddingAll(0);
        _backdrop->setBgColor(lv_color_hex(is_light ? 0x5E584A : 0x000000));
        _backdrop->setBgOpa(LV_OPA_60);
        _backdrop->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _backdrop->onClick().connect([this]() { hide(); });

        // Quick Settings card
        _card = std::make_unique<Container>(_backdrop->get());
        _card->setSize(336, 286);
        _card->align(LV_ALIGN_CENTER, 0, 12);
        _card->setRadius(24);
        _card->setBgColor(lv_color_hex(theme.cardBackground));
        _card->setBgOpa(LV_OPA_COVER);
        _card->setBorderWidth(1);
        _card->setBorderColor(lv_color_hex(theme.border));
        _card->setPaddingAll(0);
        _card->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        // Stop clicks on card from bubbling to backdrop
        _card->removeFlag(LV_OBJ_FLAG_EVENT_BUBBLE);

        // Top handle pill inside card
        _handle = std::make_unique<Container>(_card->get());
        _handle->setSize(36, 4);
        _handle->align(LV_ALIGN_TOP_MID, 0, 12);
        _handle->setRadius(2);
        _handle->setBorderWidth(0);
        _handle->setBgColor(lv_color_hex(theme.textMuted));
        _handle->setBgOpa(LV_OPA_COVER);
        _handle->removeFlag(LV_OBJ_FLAG_CLICKABLE);

        // Card title
        _title = std::make_unique<Label>(_card->get());
        _title->setText("Quick Settings");
        _title->setTextFont(&lv_font_montserrat_20);
        _title->setTextColor(lv_color_hex(theme.textPrimary));
        _title->align(LV_ALIGN_TOP_MID, 0, 26);

        // Brightness row (5% - 100%)
        _brightness_row = std::make_unique<QuickSettingRow>(
            _card->get(), 64, LV_SYMBOL_EYE_OPEN, "Brightness", 5, 100, _brightness_accent,
            [](int val, bool save) { GetHAL().setBackLightBrightness(val, save); });

        // Volume row (0% - 100%)
        _volume_row = std::make_unique<QuickSettingRow>(
            _card->get(), 154, LV_SYMBOL_VOLUME_MAX, "Volume", 0, 100, _volume_accent,
            [](int val, bool save) { GetHAL().setSpeakerVolume(val, save); });

        // Bottom close hint button
        _close_hint = std::make_unique<Container>(_card->get());
        _close_hint->setSize(220, 36);
        _close_hint->align(LV_ALIGN_BOTTOM_MID, 0, -6);
        _close_hint->setBgOpa(0);
        _close_hint->setBorderWidth(0);
        _close_hint->setPaddingAll(0);
        _close_hint->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _close_hint->onClick().connect([this]() { hide(); });

        _close_label = std::make_unique<Label>(_close_hint->get());
        _close_label->setText(LV_SYMBOL_UP "  Swipe up to close");
        _close_label->setTextFont(&lv_font_montserrat_14);
        _close_label->setTextColor(lv_color_hex(theme.textSecondary));
        _close_label->align(LV_ALIGN_CENTER, 0, 0);

        _backdrop->setHidden(true);
        _is_hidden = true;
        _pos_y_anim.springOptions().bounce         = 0.08;
        _pos_y_anim.springOptions().visualDuration = 0.25;
        _pos_y_anim.teleport(_pos_y_hide);
    }

    void update()
    {
        _pos_y_anim.update();
        if (!_pos_y_anim.done()) {
            _card->setPos(0, _pos_y_anim.directValue());
        } else if (_hide_panel_flag) {
            _hide_panel_flag = false;
            _backdrop->setHidden(true);
        }
    }

    void show()
    {
        _brightness_row->setValue(GetHAL().getBackLightBrightness());
        _volume_row->setValue(GetHAL().getSpeakerVolume());
        _backdrop->moveForeground();
        _backdrop->setHidden(false);
        _hide_panel_flag = false;
        _pos_y_anim      = _pos_y_show;
        _is_hidden       = false;
    }

    void hide()
    {
        _pos_y_anim      = _pos_y_hide;
        _is_hidden       = true;
        _hide_panel_flag = true;
    }

    bool isHidden() const
    {
        return _is_hidden;
    }

    bool isSliderDragging() const
    {
        return (_brightness_row && _brightness_row->isSliderPressed()) ||
               (_volume_row && _volume_row->isSliderPressed());
    }

private:
    const int _pos_y_show = 12;
    const int _pos_y_hide = -360;

    std::unique_ptr<Container> _backdrop;
    std::unique_ptr<Container> _card;
    std::unique_ptr<Container> _handle;
    std::unique_ptr<Label> _title;
    std::unique_ptr<QuickSettingRow> _brightness_row;
    std::unique_ptr<QuickSettingRow> _volume_row;
    std::unique_ptr<Container> _close_hint;
    std::unique_ptr<Label> _close_label;

    AnimateValue _pos_y_anim;
    bool _is_hidden       = true;
    bool _hide_panel_flag = false;
};

}  // namespace status_bar_view

/**
 * @brief Quick Settings dropdown controller (replaces legacy status bar pill).
 */
class StatusBar {
public:
    void init(lv_obj_t* parent, uint32_t colorSecondary, uint32_t colorPrimary, bool silent)
    {
        _status_bar_gesture            = std::make_unique<StatuBarGesture>();
        _status_bar_gesture->onGesture = [&]() { handle_gesture(); };
        _status_bar_gesture->onSwipeUp = [&]() {
            if (_status_bar_view && !_status_bar_view->isHidden() && !_status_bar_view->isSliderDragging()) {
                _status_bar_view->hide();
            }
        };
        _status_bar_gesture->init();

        _status_bar_view = std::make_unique<status_bar_view::StatusBarView>(parent, colorSecondary, colorPrimary);

        if (!silent) {
            _status_bar_view->show();
        }
    }

    void update()
    {
        bool open = _status_bar_view && !_status_bar_view->isHidden();
        _status_bar_gesture->update(open);
        _status_bar_view->update();
    }

    void show()
    {
        handle_gesture();
    }

    bool isHidden() const
    {
        return _status_bar_view == nullptr ? true : _status_bar_view->isHidden();
    }

private:
    std::unique_ptr<StatuBarGesture> _status_bar_gesture;
    std::unique_ptr<status_bar_view::StatusBarView> _status_bar_view;

    void handle_gesture()
    {
        if (_status_bar_view) {
            _status_bar_view->show();
        }
    }
};

namespace view {

static std::unique_ptr<StatusBar> _status_bar;

void create_status_bar(uint32_t colorSecondary, uint32_t colorPrimary, bool silent, lv_obj_t* parent)
{
    if (colorSecondary == 0xEDF4FF && colorPrimary == 0x385179) {
        colorSecondary = GetHAL().getTheme().statusBarSecondary;
        colorPrimary   = GetHAL().getTheme().statusBarPrimary;
    }
    _status_bar = std::make_unique<StatusBar>();
    _status_bar->init(parent, colorSecondary, colorPrimary, silent);
}

void update_status_bar()
{
    if (_status_bar) {
        _status_bar->update();
    }
}

void show_status_bar()
{
    if (_status_bar) {
        _status_bar->show();
    }
}

bool is_status_bar_hidden()
{
    return _status_bar == nullptr ? true : _status_bar->isHidden();
}

bool is_status_bar_created()
{
    return _status_bar != nullptr;
}

void destroy_status_bar()
{
    _status_bar.reset();
}

}  // namespace view

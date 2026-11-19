/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include "../view/arc_action_bar.h"
#include <assets/assets.h>
#include <mooncake_log.h>
#include <hal/hal.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static const std::string_view _tag = "Setup-Device";

namespace {

void style_modern_switch(Switch& sw, bool initialValue)
{
    sw.setSize(76, 42);
    sw.setValue(initialValue);
    sw.setBgColor(lv_color_hex(0x2F363D), LV_PART_MAIN);
    sw.setBgOpa(LV_OPA_COVER, LV_PART_MAIN);
    sw.setBorderWidth(0, LV_PART_MAIN);
    sw.setRadius(LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw.get(), lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw.get(), LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_width(sw.get(), 0, LV_PART_KNOB);
    lv_obj_set_style_radius(sw.get(), LV_RADIUS_CIRCLE, LV_PART_KNOB);
    sw.setBgColor(lv_color_hex(0x4AD78C), LV_PART_INDICATOR | LV_STATE_CHECKED);
    sw.setBgOpa(LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    sw.setBorderWidth(0, LV_PART_INDICATOR | LV_STATE_CHECKED);
}

void style_modern_roller(Roller& roller)
{
    lv_obj_set_style_radius(roller.get(), 48, LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller.get(), lv_color_hex(0x1B1F24), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(roller.get(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(roller.get(), 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(roller.get(), &lv_font_montserrat_26, LV_PART_MAIN);
    lv_obj_set_style_text_color(roller.get(), lv_color_hex(0x8FA0B0), LV_PART_MAIN);
    lv_obj_set_style_text_align(roller.get(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(roller.get(), 20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller.get(), lv_color_hex(0x264D3B), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller.get(), LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_font(roller.get(), &lv_font_montserrat_28, LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller.get(), lv_color_hex(0x4AD78C), LV_PART_SELECTED);
    lv_obj_set_style_text_align(roller.get(), LV_TEXT_ALIGN_CENTER, LV_PART_SELECTED);
    lv_obj_set_style_border_width(roller.get(), 0, LV_PART_SELECTED);
}

std::unique_ptr<Container> create_root_panel()
{
    auto panel = std::make_unique<Container>(lv_screen_active());
    panel->align(LV_ALIGN_CENTER, 0, 0);
    panel->setSize(466, 466);
    panel->setRadius(0);
    panel->setBorderWidth(0);
    panel->setPaddingAll(0);
    panel->setBgColor(lv_color_hex(0x000000));
    panel->setBgOpa(LV_OPA_COVER);
    panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

}  // namespace

namespace setup_workers {

class PercentageAdjustView {
public:
    PercentageAdjustView(const char* title, int initialValue, int minValue, int maxValue, int step, uint32_t accent)
    {
        _min_value     = minValue;
        _max_value     = maxValue;
        _step          = step > 0 ? step : 1;
        _current_value = normalizeValue(initialValue);

        _panel = create_root_panel();

        _value_label = std::make_unique<Label>(_panel->get());
        _value_label->align(LV_ALIGN_CENTER, -8, -42);
        _value_label->setTextFont(&CommissionerMedium108);
        _value_label->setTextColor(lv_color_hex(0xFFFFFF));

        _unit_label = std::make_unique<Label>(_panel->get());
        _unit_label->setText("%");
        _unit_label->setTextFont(&MontserratSemiBold26);
        _unit_label->setTextColor(lv_color_hex(accent));
        _unit_label->align(LV_ALIGN_CENTER, 118, -14);

        _slider = std::make_unique<Slider>(_panel->get());
        _slider->align(LV_ALIGN_CENTER, 0, 44);
        _slider->setSize(336, 30);
        _slider->setRange(_min_value, _max_value, false);
        _slider->setValue(_current_value);
        _slider->setBgColor(lv_color_hex(0x1F2429), LV_PART_MAIN);
        _slider->setBgOpa(LV_OPA_COVER, LV_PART_MAIN);
        _slider->setBorderWidth(0, LV_PART_MAIN);
        _slider->setRadius(LV_RADIUS_CIRCLE, LV_PART_MAIN);
        _slider->setBgColor(lv_color_hex(accent), LV_PART_INDICATOR);
        _slider->setBgOpa(LV_OPA_COVER, LV_PART_INDICATOR);
        _slider->setBorderWidth(0, LV_PART_INDICATOR);
        _slider->setRadius(LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(_slider->get(), lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(_slider->get(), LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_border_width(_slider->get(), 0, LV_PART_KNOB);
        lv_obj_set_style_radius(_slider->get(), LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_set_style_pad_all(_slider->get(), 6, LV_PART_KNOB);
        _slider->onValueChanged().connect([this](int32_t value) {
            if (_syncing_slider) {
                return;
            }
            setValue(static_cast<int>(value), false);
        });

        // Quick - / + step buttons in the reclaimed lower area
        const int btn_step = std::max(_step, 5);
        _minus_btn         = createStepButton(-66, 122, LV_SYMBOL_MINUS, [this, btn_step]() {
            setValue(_current_value - btn_step, true);
        });
        _plus_btn          = createStepButton(66, 122, LV_SYMBOL_PLUS, [this, btn_step]() {
            setValue(_current_value + btn_step, true);
        });

        _action_bar = std::make_unique<view::ArcActionBar>(_panel->get(), "Cancel", "OK", title);

        updateValueLabel(_current_value);
    }

    void update()
    {
        if (_action_bar) {
            _action_bar->update();
        }
    }

    int currentValue() const
    {
        return _current_value;
    }

    bool consumeSaveRequested()
    {
        return _action_bar && _action_bar->consumeRightClicked();
    }

    bool consumeCancelRequested()
    {
        return _action_bar && _action_bar->consumeLeftClicked();
    }

private:
    std::unique_ptr<Button> createStepButton(int x, int y, const char* symbol, std::function<void()> onClick)
    {
        auto btn = std::make_unique<Button>(_panel->get());
        btn->setSize(96, 56);
        btn->align(LV_ALIGN_CENTER, x, y);
        btn->setRadius(28);
        btn->setBorderWidth(0);
        btn->setShadowWidth(0);
        btn->setBgColor(lv_color_hex(0x1B1F24));
        lv_obj_set_style_bg_color(btn->get(), lv_color_hex(0x2C3238), LV_STATE_PRESSED);
        btn->label().setText(symbol);
        btn->label().setTextFont(&lv_font_montserrat_24);
        btn->label().setTextColor(lv_color_hex(0xFFFFFF));
        btn->label().align(LV_ALIGN_CENTER, 0, 0);
        btn->onClick().connect(std::move(onClick));
        return btn;
    }

    void setValue(int value, bool syncSlider)
    {
        int normalized = normalizeValue(value);
        if (syncSlider || normalized != value) {
            _syncing_slider = true;
            _slider->setValue(normalized);
            _syncing_slider = false;
        }
        _current_value = normalized;
        updateValueLabel(_current_value);
    }

    int normalizeValue(int value) const
    {
        int clamped = uitk::clamp(value, _min_value, _max_value);
        int snapped = _min_value + ((clamped - _min_value + _step / 2) / _step) * _step;
        return uitk::clamp(snapped, _min_value, _max_value);
    }

    void updateValueLabel(int value)
    {
        if (!_value_label) {
            return;
        }
        char buffer[8] = {};
        std::snprintf(buffer, sizeof(buffer), "%d", value);
        _value_label->setText(buffer);
    }

    std::unique_ptr<Container> _panel;
    std::unique_ptr<Label> _value_label;
    std::unique_ptr<Label> _unit_label;
    std::unique_ptr<Slider> _slider;
    std::unique_ptr<Button> _minus_btn;
    std::unique_ptr<Button> _plus_btn;
    std::unique_ptr<view::ArcActionBar> _action_bar;
    int _current_value   = 0;
    int _min_value       = 0;
    int _max_value       = 100;
    int _step            = 1;
    bool _syncing_slider = false;
};

class ButtonWorker::ButtonConfigView {
public:
    explicit ButtonConfigView(const Hal::ButtonConfig& initialConfig) : _current_config(initialConfig)
    {
        _panel = create_root_panel();

        createSwitchRow(148, "Button SFX", _current_config.sfxEnabled,
                        [this](bool enabled) { _current_config.sfxEnabled = enabled; });
        createSwitchRow(246, "Button Vibration", _current_config.vibrateEnabled,
                        [this](bool enabled) { _current_config.vibrateEnabled = enabled; });

        _action_bar = std::make_unique<view::ArcActionBar>(_panel->get(), "Cancel", "OK", "Button");
    }

    void update()
    {
        if (_action_bar) {
            _action_bar->update();
        }
    }

    const Hal::ButtonConfig& currentConfig() const
    {
        return _current_config;
    }

    bool consumeSaveRequested()
    {
        return _action_bar && _action_bar->consumeRightClicked();
    }

    bool consumeCancelRequested()
    {
        return _action_bar && _action_bar->consumeLeftClicked();
    }

private:
    void createSwitchRow(int y, const char* title, bool initialValue, const std::function<void(bool)>& onChanged)
    {
        auto row = std::make_unique<Container>(_panel->get());
        row->setSize(356, 82);
        row->align(LV_ALIGN_TOP_MID, 0, y);
        row->setBgColor(lv_color_hex(0x1B1F24));
        row->setBgOpa(LV_OPA_COVER);
        row->setBorderWidth(0);
        row->setShadowWidth(0);
        row->setRadius(41);
        row->setPaddingAll(0);
        row->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        auto label = std::make_unique<Label>(row->get());
        label->setText(title);
        label->setTextFont(&lv_font_montserrat_22);
        label->setTextColor(lv_color_hex(0xFFFFFF));
        label->align(LV_ALIGN_LEFT_MID, 28, 0);

        auto switch_widget = std::make_unique<Switch>(row->get());
        switch_widget->align(LV_ALIGN_RIGHT_MID, -24, 0);
        style_modern_switch(*switch_widget, initialValue);
        switch_widget->onValueChanged().connect(onChanged);

        _labels.push_back(std::move(label));
        _switches.push_back(std::move(switch_widget));
        _rows.push_back(std::move(row));
    }

    std::unique_ptr<Container> _panel;
    std::vector<std::unique_ptr<Container>> _rows;
    std::vector<std::unique_ptr<Label>> _labels;
    std::vector<std::unique_ptr<Switch>> _switches;
    std::unique_ptr<view::ArcActionBar> _action_bar;
    Hal::ButtonConfig _current_config;
};

class EyeCareWorker::EyeCareView {
public:
    explicit EyeCareView(bool initialEnabled) : _current_enabled(initialEnabled)
    {
        _panel = create_root_panel();

        createSwitchRow(148, "10m Reminder", _current_enabled,
                        [this](bool enabled) { _current_enabled = enabled; });

        createTestButtonRow(252, "Test Tone", [this]() { _test_requested = true; });

        _action_bar = std::make_unique<view::ArcActionBar>(_panel->get(), "Cancel", "OK", "Eye Care");
    }

    void update()
    {
        if (_action_bar) {
            _action_bar->update();
        }
    }

    bool currentEnabled() const
    {
        return _current_enabled;
    }

    bool consumeTestRequested()
    {
        bool requested  = _test_requested;
        _test_requested = false;
        return requested;
    }

    bool consumeSaveRequested()
    {
        return _action_bar && _action_bar->consumeRightClicked();
    }

    bool consumeCancelRequested()
    {
        return _action_bar && _action_bar->consumeLeftClicked();
    }

private:
    void createSwitchRow(int y, const char* title, bool initialValue, const std::function<void(bool)>& onChanged)
    {
        auto row = std::make_unique<Container>(_panel->get());
        row->setSize(356, 82);
        row->align(LV_ALIGN_TOP_MID, 0, y);
        row->setBgColor(lv_color_hex(0x1B1F24));
        row->setBgOpa(LV_OPA_COVER);
        row->setBorderWidth(0);
        row->setShadowWidth(0);
        row->setRadius(41);
        row->setPaddingAll(0);
        row->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        auto label = std::make_unique<Label>(row->get());
        label->setText(title);
        label->setTextFont(&lv_font_montserrat_22);
        label->setTextColor(lv_color_hex(0xFFFFFF));
        label->align(LV_ALIGN_LEFT_MID, 28, 0);

        auto switch_widget = std::make_unique<Switch>(row->get());
        switch_widget->align(LV_ALIGN_RIGHT_MID, -24, 0);
        style_modern_switch(*switch_widget, initialValue);
        switch_widget->onValueChanged().connect(onChanged);

        _labels.push_back(std::move(label));
        _switches.push_back(std::move(switch_widget));
        _rows.push_back(std::move(row));
    }

    void createTestButtonRow(int y, const char* title, const std::function<void()>& onClick)
    {
        _test_button = std::make_unique<Button>(_panel->get());
        _test_button->setSize(240, 62);
        _test_button->align(LV_ALIGN_TOP_MID, 0, y);
        _test_button->setBgColor(lv_color_hex(0x232930));
        _test_button->setBgOpa(LV_OPA_COVER);
        lv_obj_set_style_bg_color(_test_button->get(), lv_color_hex(0x323A44), LV_STATE_PRESSED);
        _test_button->setBorderWidth(0);
        _test_button->setShadowWidth(0);
        _test_button->setRadius(31);
        _test_button->label().setText(title);
        _test_button->label().setTextFont(&lv_font_montserrat_22);
        _test_button->label().setTextColor(lv_color_hex(0x81C784));
        _test_button->label().align(LV_ALIGN_CENTER, 0, 0);
        _test_button->label().removeFlag(LV_OBJ_FLAG_CLICKABLE);
        _test_button->onClick().connect(onClick);
    }

    std::unique_ptr<Container> _panel;
    std::vector<std::unique_ptr<Container>> _rows;
    std::vector<std::unique_ptr<Label>> _labels;
    std::vector<std::unique_ptr<Switch>> _switches;
    std::unique_ptr<Button> _test_button;
    std::unique_ptr<view::ArcActionBar> _action_bar;
    bool _current_enabled = false;
    bool _test_requested  = false;
};

class WifiWorker::WifiConfigView {
public:
    explicit WifiConfigView(const Hal::WifiStatus& initialStatus)
    {
        _panel = create_root_panel();

        _row = std::make_unique<Container>(_panel->get());
        _row->setSize(356, 82);
        _row->align(LV_ALIGN_TOP_MID, 0, 128);
        _row->setBgColor(lv_color_hex(0x1B1F24));
        _row->setBgOpa(LV_OPA_COVER);
        _row->setBorderWidth(0);
        _row->setShadowWidth(0);
        _row->setRadius(41);
        _row->setPaddingAll(0);
        _row->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _switch_label = std::make_unique<Label>(_row->get());
        _switch_label->setText("Wi-Fi");
        _switch_label->setTextFont(&lv_font_montserrat_22);
        _switch_label->setTextColor(lv_color_hex(0xFFFFFF));
        _switch_label->align(LV_ALIGN_LEFT_MID, 28, 0);

        _switch_widget = std::make_unique<Switch>(_row->get());
        _switch_widget->align(LV_ALIGN_RIGHT_MID, -24, 0);
        style_modern_switch(*_switch_widget, initialStatus.enabled);
        _switch_widget->onValueChanged().connect([this](bool enabled) {
            if (_syncing_switch) {
                return;
            }
            _pending_enabled = enabled;
            _has_pending     = true;
        });

        _status_card = std::make_unique<Container>(_panel->get());
        _status_card->setSize(336, 128);
        _status_card->align(LV_ALIGN_TOP_MID, 0, 228);
        _status_card->setBgColor(lv_color_hex(0x121519));
        _status_card->setBgOpa(LV_OPA_COVER);
        _status_card->setBorderWidth(0);
        _status_card->setShadowWidth(0);
        _status_card->setRadius(32);
        _status_card->setPaddingAll(16);
        _status_card->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _status_label = std::make_unique<Label>(_status_card->get());
        _status_label->setTextFont(&lv_font_montserrat_20);
        _status_label->setTextColor(lv_color_hex(0xC0CCD8));
        _status_label->setWidth(300);
        _status_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _status_label->setLongMode(LV_LABEL_LONG_MODE_WRAP);
        _status_label->align(LV_ALIGN_CENTER, 0, 0);

        _action_bar = std::make_unique<view::ArcActionBar>(_panel->get(), "Back", "OK", "Wi-Fi", 0x9EB3C2, 0x4AD78C);

        updateStatus(initialStatus);
    }

    void update()
    {
        if (_action_bar) {
            _action_bar->update();
        }
    }

    bool consumeEnabledChange(bool& enabled)
    {
        if (!_has_pending) {
            return false;
        }
        enabled      = _pending_enabled;
        _has_pending = false;
        return true;
    }

    bool consumeDoneRequested()
    {
        if (!_action_bar) {
            return false;
        }
        return _action_bar->consumeLeftClicked() || _action_bar->consumeRightClicked();
    }

    void updateStatus(const Hal::WifiStatus& status)
    {
        if (_switch_widget) {
            _syncing_switch = true;
            _switch_widget->setValue(status.enabled);
            _syncing_switch = false;
        }
        if (_status_label) {
            _status_label->setText(buildStatusText(status));
        }
    }

private:
    std::string buildStatusText(const Hal::WifiStatus& status) const
    {
        switch (status.mode) {
        case Hal::WifiMode::Off:
            return "Wi-Fi is Off";
        case Hal::WifiMode::Connecting:
            if (!status.ssid.empty()) {
                return "Connecting...\n" + status.ssid;
            }
            return status.message.empty() ? "Connecting..." : status.message;
        case Hal::WifiMode::Connected:
            if (!status.ipAddress.empty()) {
                return "Connected\n" + status.ssid + "\n" + status.ipAddress;
            }
            return "Connected\n" + status.ssid;
        case Hal::WifiMode::ConfigAp:
            return "Setup AP\n" + status.apSsid + "\n192.168.4.1";
        case Hal::WifiMode::BadgeAp:
            return "Badge AP active";
        case Hal::WifiMode::Error:
            return status.message.empty() ? "Wi-Fi error" : status.message;
        }
        return status.message;
    }

    std::unique_ptr<Container> _panel;
    std::unique_ptr<Container> _row;
    std::unique_ptr<Label> _switch_label;
    std::unique_ptr<Switch> _switch_widget;
    std::unique_ptr<Container> _status_card;
    std::unique_ptr<Label> _status_label;
    std::unique_ptr<view::ArcActionBar> _action_bar;
    bool _syncing_switch  = false;
    bool _pending_enabled = false;
    bool _has_pending     = false;
};

}  // namespace setup_workers

BrightnessWorker::BrightnessWorker()
{
    mclog::tagInfo(_tag, "start brightness worker");

    _initial_brightness = GetHAL().getBackLightBrightness();
    _applied_brightness = _initial_brightness;
    _view               = std::make_unique<PercentageAdjustView>("Brightness", _applied_brightness, 10, 100, 1, 0xFFB74D);
}

void BrightnessWorker::update()
{
    if (!_view) {
        return;
    }
    _view->update();

    if (_applied_brightness != _view->currentValue()) {
        GetHAL().setBackLightBrightness(_view->currentValue(), false);
        _applied_brightness = _view->currentValue();
    }

    if (_view->consumeCancelRequested()) {
        _is_done = true;
        return;
    }

    if (_view->consumeSaveRequested()) {
        GetHAL().setBackLightBrightness(_view->currentValue(), true);
        _saved   = true;
        _is_done = true;
    }
}

BrightnessWorker::~BrightnessWorker()
{
    if (!_saved) {
        GetHAL().setBackLightBrightness(_initial_brightness, false);
    }
}

class ScreenTimeoutWorker::ScreenTimeoutView {
public:
    explicit ScreenTimeoutView(uint32_t initialTimeout)
    {
        _selected_timeout = initialTimeout;
        _panel            = create_root_panel();

        _roller = std::make_unique<Roller>(_panel->get());
        _roller->align(LV_ALIGN_CENTER, 0, 22);
        _roller->setSize(280, 240);
        _roller->setOptions("15s\n30s\n1 min\n2 min\n5 min\nNever", LV_ROLLER_MODE_INFINITE);
        _roller->setVisibleRowCount(5);
        style_modern_roller(*_roller);

        uint32_t sel_idx = 1;
        if (initialTimeout == 15) {
            sel_idx = 0;
        } else if (initialTimeout == 30) {
            sel_idx = 1;
        } else if (initialTimeout == 60) {
            sel_idx = 2;
        } else if (initialTimeout == 120) {
            sel_idx = 3;
        } else if (initialTimeout == 300) {
            sel_idx = 4;
        } else if (initialTimeout == 0) {
            sel_idx = 5;
        }

        _roller->setSelected(sel_idx, LV_ANIM_OFF);
        _roller->onValueChanged().connect([this](uint32_t index) {
            static const uint32_t timeouts[] = {15, 30, 60, 120, 300, 0};
            if (index < 6) {
                _selected_timeout = timeouts[index];
            }
        });

        _action_bar = std::make_unique<view::ArcActionBar>(_panel->get(), "Cancel", "OK", "Timeout");
    }

    void update()
    {
        if (_action_bar) {
            _action_bar->update();
        }
    }

    uint32_t selectedTimeout() const
    {
        return _selected_timeout;
    }

    bool consumeSaveRequested()
    {
        return _action_bar && _action_bar->consumeRightClicked();
    }

    bool consumeCancelRequested()
    {
        return _action_bar && _action_bar->consumeLeftClicked();
    }

private:
    std::unique_ptr<Container> _panel;
    std::unique_ptr<Roller> _roller;
    std::unique_ptr<view::ArcActionBar> _action_bar;
    uint32_t _selected_timeout = 30;
};

ScreenTimeoutWorker::ScreenTimeoutWorker()
{
    mclog::tagInfo(_tag, "start screen timeout worker");
    _applied_timeout = GetHAL().getScreenTimeout();
    _view            = std::make_unique<ScreenTimeoutView>(_applied_timeout);
}

void ScreenTimeoutWorker::update()
{
    if (!_view) {
        return;
    }
    _view->update();

    if (_view->consumeCancelRequested()) {
        _is_done = true;
        return;
    }

    if (_view->consumeSaveRequested()) {
        GetHAL().setScreenTimeout(_view->selectedTimeout(), true);
        _is_done = true;
    }
}

ScreenTimeoutWorker::~ScreenTimeoutWorker() = default;

class ColorTempWorker::ColorTempView {
public:
    explicit ColorTempView(Hal::ColorTemp initialTemp)
    {
        _selected_temp = initialTemp;
        _panel         = create_root_panel();

        _roller = std::make_unique<Roller>(_panel->get());
        _roller->align(LV_ALIGN_CENTER, 0, 22);
        _roller->setSize(300, 220);
        _roller->setOptions("Cool (Native)\nSoft Warm\nWarm", LV_ROLLER_MODE_NORMAL);
        _roller->setVisibleRowCount(3);
        style_modern_roller(*_roller);

        uint32_t sel_idx = static_cast<uint32_t>(initialTemp);
        if (sel_idx > 2) {
            sel_idx = 0;
        }

        _roller->setSelected(sel_idx, LV_ANIM_OFF);
        _roller->onValueChanged().connect([this](uint32_t index) {
            if (index <= 2) {
                _selected_temp = static_cast<Hal::ColorTemp>(index);
            }
        });

        _action_bar = std::make_unique<view::ArcActionBar>(_panel->get(), "Cancel", "OK", "Color Temp");
    }

    void update()
    {
        if (_action_bar) {
            _action_bar->update();
        }
    }

    Hal::ColorTemp selectedColorTemp() const
    {
        return _selected_temp;
    }

    bool consumeSaveRequested()
    {
        return _action_bar && _action_bar->consumeRightClicked();
    }

    bool consumeCancelRequested()
    {
        return _action_bar && _action_bar->consumeLeftClicked();
    }

private:
    std::unique_ptr<Container> _panel;
    std::unique_ptr<Roller> _roller;
    std::unique_ptr<view::ArcActionBar> _action_bar;
    Hal::ColorTemp _selected_temp = Hal::ColorTemp::Cool;
};

ColorTempWorker::ColorTempWorker()
{
    mclog::tagInfo(_tag, "start color temp worker");
    _applied_temp = GetHAL().getColorTemperature();
    _initial_temp = _applied_temp;
    _view         = std::make_unique<ColorTempView>(_applied_temp);
}

void ColorTempWorker::update()
{
    if (!_view) {
        return;
    }
    _view->update();

    if (_applied_temp != _view->selectedColorTemp()) {
        GetHAL().setColorTemperature(_view->selectedColorTemp(), false);
        _applied_temp = _view->selectedColorTemp();
        lv_obj_invalidate(lv_screen_active());
    }

    if (_view->consumeCancelRequested()) {
        _is_done = true;
        return;
    }

    if (_view->consumeSaveRequested()) {
        GetHAL().setColorTemperature(_view->selectedColorTemp(), true);
        _saved   = true;
        _is_done = true;
    }
}

ColorTempWorker::~ColorTempWorker()
{
    if (!_saved) {
        GetHAL().setColorTemperature(_initial_temp, false);
        lv_obj_invalidate(lv_screen_active());
    }
}

VolumeWorker::VolumeWorker()
{
    mclog::tagInfo(_tag, "start volume worker");

    _initial_volume = GetHAL().getSpeakerVolume();
    _applied_volume = _initial_volume;
    _view           = std::make_unique<PercentageAdjustView>("Volume", _applied_volume, 0, 100, 5, 0x4FC3F7);
}

void VolumeWorker::update()
{
    if (!_view) {
        return;
    }
    _view->update();

    if (_applied_volume != _view->currentValue()) {
        GetHAL().setSpeakerVolume(_view->currentValue(), false);
        _applied_volume = _view->currentValue();
    }

    if (_view->consumeCancelRequested()) {
        _is_done = true;
        return;
    }

    if (_view->consumeSaveRequested()) {
        GetHAL().setSpeakerVolume(_view->currentValue(), true);
        _saved   = true;
        _is_done = true;
    }
}

VolumeWorker::~VolumeWorker()
{
    if (!_saved) {
        GetHAL().setSpeakerVolume(_initial_volume, false);
    }
}

ButtonWorker::ButtonWorker()
{
    mclog::tagInfo(_tag, "start button worker");

    _initial_config = GetHAL().getButtonConfig();
    _applied_config = _initial_config;
    _view           = std::make_unique<ButtonConfigView>(_applied_config);
}

void ButtonWorker::update()
{
    if (!_view) {
        return;
    }
    _view->update();

    if (_applied_config.sfxEnabled != _view->currentConfig().sfxEnabled ||
        _applied_config.vibrateEnabled != _view->currentConfig().vibrateEnabled) {
        GetHAL().setButtonConfig(_view->currentConfig(), false);
        _applied_config = _view->currentConfig();
    }

    if (_view->consumeCancelRequested()) {
        _is_done = true;
        return;
    }

    if (_view->consumeSaveRequested()) {
        GetHAL().setButtonConfig(_view->currentConfig(), true);
        _saved   = true;
        _is_done = true;
    }
}

ButtonWorker::~ButtonWorker()
{
    if (!_saved) {
        GetHAL().setButtonConfig(_initial_config, false);
    }
}

EyeCareWorker::EyeCareWorker()
{
    mclog::tagInfo(_tag, "start eye care worker");

    _applied_enabled = GetHAL().getEyeCareReminder();
    _initial_enabled = _applied_enabled;
    _view            = std::make_unique<EyeCareView>(_applied_enabled);
}

void EyeCareWorker::update()
{
    if (!_view) {
        return;
    }
    _view->update();

    if (_view->consumeTestRequested()) {
        GetHAL().playEyeCareDing();
    }

    if (_applied_enabled != _view->currentEnabled()) {
        GetHAL().setEyeCareReminder(_view->currentEnabled(), false);
        _applied_enabled = _view->currentEnabled();
    }

    if (_view->consumeCancelRequested()) {
        _is_done = true;
        return;
    }

    if (_view->consumeSaveRequested()) {
        GetHAL().setEyeCareReminder(_view->currentEnabled(), true);
        _saved   = true;
        _is_done = true;
    }
}

EyeCareWorker::~EyeCareWorker()
{
    if (!_saved) {
        GetHAL().setEyeCareReminder(_initial_enabled, false);
    }
}

WifiWorker::WifiWorker()
{
    mclog::tagInfo(_tag, "start wifi worker");

    _last_status = GetHAL().getWifiStatus();
    _view        = std::make_unique<WifiConfigView>(_last_status);
}

void WifiWorker::update()
{
    if (!_view) {
        return;
    }
    _view->update();

    bool enabled = false;
    if (_view->consumeEnabledChange(enabled)) {
        GetHAL().setWifiEnabled(enabled, true);
    }

    const auto status = GetHAL().getWifiStatus();
    _view->updateStatus(status);
    _last_status = status;

    if (_view->consumeDoneRequested()) {
        _is_done = true;
    }
}

WifiWorker::~WifiWorker() = default;

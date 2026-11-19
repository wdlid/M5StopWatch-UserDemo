/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include "../view/arc_action_bar.h"
#include <assets/assets.h>
#include <mooncake_log.h>
#include <cstdio>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static const std::string_view _tag = "Setup-DateTime";

namespace setup_workers {

namespace {

std::string build_number_options(int begin, int end, int width = 2)
{
    std::string options;
    for (int value = begin; value <= end; ++value) {
        char buffer[8] = {};
        std::snprintf(buffer, sizeof(buffer), "%0*d", width, value);
        if (!options.empty()) {
            options.push_back('\n');
        }
        options += buffer;
    }
    return options;
}

void apply_selector_style(Roller& roller)
{
    lv_obj_set_style_radius(roller.get(), 42, LV_PART_MAIN);
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

class SetTimeWorker::TimeAdjustView {
public:
    explicit TimeAdjustView(const TimeHms& initialTime)
    {
        _current_time = initialTime;
        _panel        = create_root_panel();

        _summary_label = std::make_unique<Label>(_panel->get());
        _summary_label->align(LV_ALIGN_TOP_MID, 0, 96);
        _summary_label->setTextFont(&lv_font_maple_mono_medium_24);
        _summary_label->setTextColor(lv_color_hex(0x9FB0C0));

        const auto hour_options = build_number_options(0, 23);
        _hour_selector          = std::make_unique<Roller>(_panel->get());
        _hour_selector->align(LV_ALIGN_CENTER, -112, 26);
        _hour_selector->setSize(102, 224);
        _hour_selector->setOptions(hour_options.c_str(), LV_ROLLER_MODE_INFINITE);
        _hour_selector->setVisibleRowCount(5);
        _hour_selector->setSelected(_current_time.hour, LV_ANIM_OFF);
        apply_selector_style(*_hour_selector);
        _hour_selector->onValueChanged().connect([this](uint32_t value) {
            _current_time.hour = static_cast<uint8_t>(value);
            updateSummaryLabel();
        });

        const auto minute_options = build_number_options(0, 59);
        _minute_selector          = std::make_unique<Roller>(_panel->get());
        _minute_selector->align(LV_ALIGN_CENTER, 0, 26);
        _minute_selector->setSize(102, 224);
        _minute_selector->setOptions(minute_options.c_str(), LV_ROLLER_MODE_INFINITE);
        _minute_selector->setVisibleRowCount(5);
        _minute_selector->setSelected(_current_time.minute, LV_ANIM_OFF);
        apply_selector_style(*_minute_selector);
        _minute_selector->onValueChanged().connect([this](uint32_t value) {
            _current_time.minute = static_cast<uint8_t>(value);
            updateSummaryLabel();
        });

        const auto second_options = build_number_options(0, 59);
        _second_selector          = std::make_unique<Roller>(_panel->get());
        _second_selector->align(LV_ALIGN_CENTER, 112, 26);
        _second_selector->setSize(102, 224);
        _second_selector->setOptions(second_options.c_str(), LV_ROLLER_MODE_INFINITE);
        _second_selector->setVisibleRowCount(5);
        _second_selector->setSelected(_current_time.second, LV_ANIM_OFF);
        apply_selector_style(*_second_selector);
        _second_selector->onValueChanged().connect([this](uint32_t value) {
            _current_time.second = static_cast<uint8_t>(value);
            updateSummaryLabel();
        });

        _action_bar = std::make_unique<view::ArcActionBar>(_panel->get(), "Cancel", "OK", "Set Time");
        updateSummaryLabel();
    }

    void update()
    {
        if (_action_bar) {
            _action_bar->update();
        }
    }

    TimeHms currentTime() const
    {
        return _current_time;
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
    void updateSummaryLabel()
    {
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u", _current_time.hour, _current_time.minute,
                      _current_time.second);
        _summary_label->setText(buffer);
    }

    std::unique_ptr<Container> _panel;
    std::unique_ptr<Label> _summary_label;
    std::unique_ptr<Roller> _hour_selector;
    std::unique_ptr<Roller> _minute_selector;
    std::unique_ptr<Roller> _second_selector;
    std::unique_ptr<view::ArcActionBar> _action_bar;
    TimeHms _current_time;
};

class SetDateWorker::DateAdjustView {
public:
    explicit DateAdjustView(const DateYmd& initialDate)
    {
        _current_date = initialDate;
        _panel        = create_root_panel();

        _summary_label = std::make_unique<Label>(_panel->get());
        _summary_label->align(LV_ALIGN_TOP_MID, 0, 96);
        _summary_label->setTextFont(&lv_font_maple_mono_medium_24);
        _summary_label->setTextColor(lv_color_hex(0x9FB0C0));

        const auto year_options = build_number_options(2000, 2099, 4);
        _year_selector          = std::make_unique<Roller>(_panel->get());
        _year_selector->align(LV_ALIGN_CENTER, -72, 26);
        _year_selector->setSize(166, 224);
        _year_selector->setOptions(year_options.c_str(), LV_ROLLER_MODE_INFINITE);
        _year_selector->setVisibleRowCount(5);
        _year_selector->setSelected(_current_date.year - 2000, LV_ANIM_OFF);
        apply_selector_style(*_year_selector);
        _year_selector->onValueChanged().connect([this](uint32_t value) {
            _current_date.year = static_cast<uint16_t>(2000 + value);
            syncDateValidity();
            updateSummaryLabel();
        });

        const auto month_options = build_number_options(1, 12);
        _month_selector          = std::make_unique<Roller>(_panel->get());
        _month_selector->align(LV_ALIGN_CENTER, 92, 26);
        _month_selector->setSize(110, 224);
        _month_selector->setOptions(month_options.c_str(), LV_ROLLER_MODE_INFINITE);
        _month_selector->setVisibleRowCount(5);
        _month_selector->setSelected(_current_date.month - 1, LV_ANIM_OFF);
        apply_selector_style(*_month_selector);
        _month_selector->onValueChanged().connect([this](uint32_t value) {
            _current_date.month = static_cast<uint8_t>(value + 1);
            syncDateValidity();
            updateSummaryLabel();
        });

        _day_selector = std::make_unique<Roller>(_panel->get());
        _day_selector->align(LV_ALIGN_CENTER, 0, 26);
        _day_selector->setSize(148, 224);
        _day_selector->setVisibleRowCount(5);
        apply_selector_style(*_day_selector);
        _day_selector->onValueChanged().connect([this](uint32_t value) {
            _current_date.day = static_cast<uint8_t>(value + 1);
            syncDateValidity();
            updateSummaryLabel();
        });
        syncDateValidity();

        _action_bar = std::make_unique<view::ArcActionBar>(_panel->get(), "Cancel", "Next", "Set Date");
        setStage(Stage::YearMonth);
    }

    void update()
    {
        if (!_action_bar) {
            return;
        }
        _action_bar->update();

        if (_action_bar->consumeLeftClicked()) {
            if (_stage == Stage::Day) {
                setStage(Stage::YearMonth);
            } else {
                _cancel_requested = true;
            }
        }

        if (_action_bar->consumeRightClicked()) {
            syncDateValidity();
            if (_stage == Stage::YearMonth) {
                setStage(Stage::Day);
            } else {
                _save_requested = _current_date.isValid();
            }
        }
    }

    DateYmd currentDate() const
    {
        return _current_date;
    }

    bool consumeSaveRequested()
    {
        bool requested  = _save_requested;
        _save_requested = false;
        return requested;
    }

    bool consumeCancelRequested()
    {
        bool requested    = _cancel_requested;
        _cancel_requested = false;
        return requested;
    }

private:
    enum class Stage {
        YearMonth,
        Day,
    };

    void syncDateValidity()
    {
        const uint8_t max_day = DateYmd::daysInMonth(_current_date.year, _current_date.month);
        if (_current_date.day > max_day) {
            _current_date.day = max_day;
        }
        if (_current_date.day < 1) {
            _current_date.day = 1;
        }

        const auto day_options = build_number_options(1, max_day);
        _day_selector->setOptions(day_options.c_str(), LV_ROLLER_MODE_INFINITE);
        _day_selector->setSelected(_current_date.day - 1, LV_ANIM_OFF);
    }

    void updateSummaryLabel()
    {
        char buffer[16] = {};
        if (_stage == Stage::YearMonth) {
            std::snprintf(buffer, sizeof(buffer), "%04u-%02u", _current_date.year, _current_date.month);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", _current_date.year, _current_date.month,
                          _current_date.day);
        }
        _summary_label->setText(buffer);
    }

    void setStage(Stage stage)
    {
        _stage = stage;
        updateSummaryLabel();

        const bool show_year_month = _stage == Stage::YearMonth;
        _year_selector->setHidden(!show_year_month);
        _month_selector->setHidden(!show_year_month);
        _day_selector->setHidden(show_year_month);

        if (_action_bar) {
            if (show_year_month) {
                _action_bar->setLeftText("Cancel", 0xFF8A8A);
                _action_bar->setRightText("Next", 0x4AD78C);
            } else {
                _action_bar->setLeftText("Back", 0x9EB3C2);
                _action_bar->setRightText("OK", 0x4AD78C);
            }
        }
    }

    std::unique_ptr<Container> _panel;
    std::unique_ptr<Label> _summary_label;
    std::unique_ptr<Roller> _year_selector;
    std::unique_ptr<Roller> _month_selector;
    std::unique_ptr<Roller> _day_selector;
    std::unique_ptr<view::ArcActionBar> _action_bar;
    DateYmd _current_date;
    Stage _stage           = Stage::YearMonth;
    bool _save_requested   = false;
    bool _cancel_requested = false;
};

}  // namespace setup_workers

SetTimeWorker::SetTimeWorker()
{
    mclog::tagInfo(_tag, "start set time worker");

    _applied_time = GetHAL().getTimeHms();
    _view         = std::make_unique<TimeAdjustView>(_applied_time);
}

void SetTimeWorker::update()
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
        GetHAL().setTimeHms(_view->currentTime());
        _is_done = true;
    }
}

SetTimeWorker::~SetTimeWorker() = default;

SetDateWorker::SetDateWorker()
{
    mclog::tagInfo(_tag, "start set date worker");

    _applied_date = GetHAL().getDateYmd();
    _view         = std::make_unique<DateAdjustView>(_applied_date);
}

void SetDateWorker::update()
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
        GetHAL().setDateYmd(_view->currentDate());
        _is_done = true;
    }
}

SetDateWorker::~SetDateWorker() = default;

class TimezoneWorker::TimezoneView {
public:
    explicit TimezoneView(const std::string& initialTz)
    {
        _selected_tz = initialTz;
        _panel       = create_root_panel();

        _roller = std::make_unique<Roller>(_panel->get());
        _roller->align(LV_ALIGN_CENTER, 0, 22);
        _roller->setSize(344, 240);
        _roller->setOptions(
            "UTC+8 (Beijing/HK)\nUTC+9 (Tokyo/Seoul)\nUTC+0 (London/GMT)\nUTC+1 (Berlin/Paris)\nUTC-5 (New "
            "York)\nUTC-8 (Los Angeles)",
            LV_ROLLER_MODE_INFINITE);
        _roller->setVisibleRowCount(5);
        apply_selector_style(*_roller);

        uint32_t sel_idx = 0;
        if (initialTz == "CST-8") {
            sel_idx = 0;
        } else if (initialTz == "JST-9") {
            sel_idx = 1;
        } else if (initialTz == "GMT0") {
            sel_idx = 2;
        } else if (initialTz.find("CET") != std::string::npos) {
            sel_idx = 3;
        } else if (initialTz.find("EST") != std::string::npos) {
            sel_idx = 4;
        } else if (initialTz.find("PST") != std::string::npos) {
            sel_idx = 5;
        }

        _roller->setSelected(sel_idx, LV_ANIM_OFF);
        _roller->onValueChanged().connect([this](uint32_t index) {
            static const char* tz_values[] = {
                "CST-8",
                "JST-9",
                "GMT0",
                "CET-1CEST,M3.5.0,M10.5.0/3",
                "EST5EDT,M3.2.0,M11.1.0",
                "PST8PDT,M3.2.0,M11.1.0"};
            if (index < 6) {
                _selected_tz = tz_values[index];
            }
        });

        _action_bar = std::make_unique<view::ArcActionBar>(_panel->get(), "Cancel", "OK", "Time Zone");
    }

    void update()
    {
        if (_action_bar) {
            _action_bar->update();
        }
    }

    std::string selectedTz() const
    {
        return _selected_tz;
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
    std::string _selected_tz = "CST-8";
};

TimezoneWorker::TimezoneWorker()
{
    mclog::tagInfo(_tag, "start timezone worker");
    _applied_tz = GetHAL().getTimezone();
    _view       = std::make_unique<TimezoneView>(_applied_tz);
}

void TimezoneWorker::update()
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
        GetHAL().setTimezone(_view->selectedTz());
        _is_done = true;
    }
}

TimezoneWorker::~TimezoneWorker() = default;

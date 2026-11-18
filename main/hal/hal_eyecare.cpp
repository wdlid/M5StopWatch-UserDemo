/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/settings/settings.h"
#include <mooncake_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cmath>
#include <vector>

static const std::string_view _tag = "HAL-EyeCare";

static bool _eye_care_enabled            = false;
static int _last_eye_reminder_minute     = -1;
static uint32_t _last_eye_check_ms       = 0;

void Hal::setEyeCareReminder(bool enabled, bool saveToSettings)
{
    _eye_care_enabled = enabled;
    if (enabled) {
        auto time = getTimeHms();
        _last_eye_reminder_minute = (time.minute % 10 == 0) ? time.minute : -1;
    } else {
        _last_eye_reminder_minute = -1;
    }

    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetBool("eye_care", enabled);
        mclog::tagInfo(_tag, "eye care reminder saved to settings: {}", enabled);
    }
}

bool Hal::getEyeCareReminder(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _eye_care_enabled = settings.GetBool("eye_care", false);
        if (_eye_care_enabled) {
            auto time = getTimeHms();
            _last_eye_reminder_minute = (time.minute % 10 == 0) ? time.minute : -1;
        } else {
            _last_eye_reminder_minute = -1;
        }
        mclog::tagInfo(_tag, "eye care reminder loaded from settings: {}", _eye_care_enabled);
    }
    return _eye_care_enabled;
}

void Hal::eye_care_tick()
{
    if (!_eye_care_enabled) {
        return;
    }

    const uint32_t now = millis();
    if (now - _last_eye_check_ms < 1000) {
        return;
    }
    _last_eye_check_ms = now;

    auto time = getTimeHms();
    if (time.minute % 10 == 0) {
        if (_last_eye_reminder_minute != time.minute) {
            _last_eye_reminder_minute = time.minute;
            mclog::tagInfo(_tag, "eye care reminder ding triggered at {:02d}:{:02d}", time.hour, time.minute);
            playEyeCareDing();
        }
    } else {
        _last_eye_reminder_minute = -1;
    }
}

void Hal::playEyeCareDing()
{
    mclog::tagInfo(_tag, "playing eye care reminder ding (boot sfx)");
    playBootSfx();
}

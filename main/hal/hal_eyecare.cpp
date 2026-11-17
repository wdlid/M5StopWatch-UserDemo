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
    mclog::tagInfo(_tag, "playing eye care ding at max volume");

    xTaskCreate(
        [](void*) {
            // Wait briefly if other audio is playing
            for (int i = 0; i < 20; ++i) {
                if (!GetHAL().isAudioPlaying()) {
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            const int sample_rate = GetHAL().getAudioSampleRate();
            const float duration  = 0.38f;
            const int samples     = static_cast<int>(sample_rate * duration);
            std::vector<int16_t> buffer(samples);

            // Pleasant bell chime (ding):
            // C7 (2093 Hz) fundamental + overtone E7 (~2637 Hz) + octave C8 (4186 Hz)
            const float f1    = 2093.0f;
            const float f2    = 2637.0f;
            const float f3    = 4186.0f;
            const float step1 = 2.0f * (float)M_PI * f1 / sample_rate;
            const float step2 = 2.0f * (float)M_PI * f2 / sample_rate;
            const float step3 = 2.0f * (float)M_PI * f3 / sample_rate;
            float phase1      = 0.0f;
            float phase2      = 0.0f;
            float phase3      = 0.0f;

            const int attack_samples = static_cast<int>(sample_rate * 0.005f);

            for (int i = 0; i < samples; ++i) {
                float env = 1.0f;
                if (i < attack_samples) {
                    env = (float)i / attack_samples;
                } else {
                    float decay_t = (float)(i - attack_samples) / sample_rate;
                    env           = expf(-decay_t * 8.5f);
                }

                float s1 = sinf(phase1);
                float s2 = sinf(phase2);
                float s3 = sinf(phase3);

                phase1 += step1;
                phase2 += step2;
                phase3 += step3;
                if (phase1 > 2.0f * (float)M_PI) phase1 -= 2.0f * (float)M_PI;
                if (phase2 > 2.0f * (float)M_PI) phase2 -= 2.0f * (float)M_PI;
                if (phase3 > 2.0f * (float)M_PI) phase3 -= 2.0f * (float)M_PI;

                float val = (s1 * 0.65f + s2 * 0.20f + s3 * 0.15f) * env * 29000.0f;
                buffer[i] = static_cast<int16_t>(val);
            }

            const int prev_volume = GetHAL().getSpeakerVolume();
            GetHAL().setSpeakerVolume(100, false);
            GetHAL().audioPlay(buffer, false);
            GetHAL().setSpeakerVolume(prev_volume, false);

            vTaskDelete(NULL);
        },
        "eye_ding", 4 * 1024, NULL, 5, NULL);
}

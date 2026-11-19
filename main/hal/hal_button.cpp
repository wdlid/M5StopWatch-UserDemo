/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/settings/settings.h"
#include <apps/common/audio/audio.h>
#include <mooncake_log.h>
#include <driver/gpio.h>
#include <memory>

static const std::string_view _tag = "HAL-BTN";

// USER BUTTON
#define USER_BUTTONA_PIN (gpio_num_t)2
#define USER_BUTTONB_PIN (gpio_num_t)1
#define BOOT_BUTTON_PIN  (gpio_num_t)0

void Hal::button_init()
{
    mclog::tagInfo(_tag, "init");

    gpio_reset_pin(USER_BUTTONA_PIN);
    gpio_set_direction(USER_BUTTONA_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(USER_BUTTONA_PIN, GPIO_PULLUP_ONLY);

    gpio_reset_pin(USER_BUTTONB_PIN);
    gpio_set_direction(USER_BUTTONB_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(USER_BUTTONB_PIN, GPIO_PULLUP_ONLY);

    gpio_reset_pin(BOOT_BUTTON_PIN);
    gpio_set_direction(BOOT_BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOOT_BUTTON_PIN, GPIO_PULLUP_ONLY);

    btnPwr.setHoldThresh(500);

    // Load config from settings
    getButtonConfig(true);
}

void Hal::pwr_button_tick()
{
    static uint32_t _last_poll = 0;
    static bool shutdown_triggered = false;

    const uint32_t now = millis();
    if (now - _last_poll < 20) {
        return;
    }
    _last_poll = now;

    const bool pwr_raw = pmic_get_pwr_btn_state() || !gpio_get_level(BOOT_BUTTON_PIN);

    if (isScreenSleeping()) {
        if (pwr_raw) {
            wakeScreen();
            btnPwr.ignoreUntilRelease();
            btnPwr.setRawState(now, pwr_raw);
            pmic_get_double_click_flag();
        }
        return;
    }

    btnPwr.setRawState(now, pwr_raw);

    auto& config = getButtonConfig();

    if (btnPwr.wasPressed()) {
        resetActivityTimer();
    }

    // Long press (>= 2.0s): Power off / 关机
    if (btnPwr.pressedFor(2000)) {
        if (!shutdown_triggered) {
            shutdown_triggered = true;
            mclog::tagInfo(_tag, "power/boot button held 2s: power off");
            if (config.sfxEnabled) {
                audio::play_tone(800, 0.15);
            }
            if (config.vibrateEnabled) {
                vibrate(200, 100);
            }

            // Wait for user to release the button so it doesn't wake immediately
            const uint32_t wait_start = millis();
            while ((!gpio_get_level(BOOT_BUTTON_PIN) || pmic_get_pwr_btn_state()) && (millis() - wait_start < 2500)) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            powerOff();
        }
    } else {
        if (!btnPwr.isPressed()) {
            shutdown_triggered = false;
        }

        // Double click: Sleep screen / 熄屏
        bool double_clicked = (btnPwr.wasClicked() && btnPwr.getClickCount() >= 2) ||
                              btnPwr.wasDoubleClicked() ||
                              pmic_get_double_click_flag();

        if (double_clicked) {
            btnPwr.clearClickCount();
            if (!isScreenSleeping() && (now - getLastWakeTime() > 800)) {
                mclog::tagInfo(_tag, "power/boot button double clicked: sleep screen");
                if (config.sfxEnabled) {
                    audio::play_tone(1500, 0.04);
                }
                if (config.vibrateEnabled) {
                    vibrate(30, 80);
                }
                sleepScreen();
            }
        }
    }
}

void Hal::button_on_sleep()
{
    btnA.reset();
    btnB.reset();
    btnPwr.reset();

    if (!gpio_get_level(USER_BUTTONA_PIN)) {
        btnA.ignoreUntilRelease();
    }
    if (!gpio_get_level(USER_BUTTONB_PIN)) {
        btnB.ignoreUntilRelease();
    }
    if (!gpio_get_level(BOOT_BUTTON_PIN) || pmic_get_pwr_btn_state()) {
        btnPwr.ignoreUntilRelease();
    }
    pmic_get_double_click_flag();
}

void Hal::updateButtonStates()
{
    const uint32_t now = millis();
    const bool rawA    = !gpio_get_level(USER_BUTTONA_PIN);
    const bool rawB    = !gpio_get_level(USER_BUTTONB_PIN);

    if (isScreenSleeping()) {
        const bool rawPwr = !gpio_get_level(BOOT_BUTTON_PIN) || pmic_get_pwr_btn_state();
        if (rawA || rawB || rawPwr) {
            wakeScreen();
            if (rawA) {
                btnA.ignoreUntilRelease();
            }
            if (rawB) {
                btnB.ignoreUntilRelease();
            }
            if (rawPwr) {
                btnPwr.ignoreUntilRelease();
                pmic_get_double_click_flag();
            }
            btnA.setRawState(now, rawA);
            btnB.setRawState(now, rawB);
            btnPwr.setRawState(now, rawPwr);
        }
        return;
    }

    btnA.setRawState(now, rawA);
    btnB.setRawState(now, rawB);

    pwr_button_tick();

    auto& config = getButtonConfig();
    if (btnA.wasPressed() || btnB.wasPressed()) {
        resetActivityTimer();
    }
    if (btnA.wasPressed()) {
        if (config.sfxEnabled) {
            audio::play_tone_from_midi(62 + 32, 0.02);
        }
        if (config.vibrateEnabled) {
            vibrate(20, 60);
        }
    } else if (btnB.wasPressed()) {
        if (config.sfxEnabled) {
            audio::play_tone_from_midi(64 + 32, 0.02);
        }
        if (config.vibrateEnabled) {
            vibrate(20, 60);
        }
    }
}

void Hal::setButtonConfig(ButtonConfig config, bool saveToSettings)
{
    _btn_config = config;
    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetBool("btn_sfx", config.sfxEnabled);
        settings.SetBool("btn_vibrate", config.vibrateEnabled);
        mclog::tagInfo(_tag, "config saved to settings: sfx={}, vibrate={}", config.sfxEnabled, config.vibrateEnabled);
    }
}

const Hal::ButtonConfig& Hal::getButtonConfig(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _btn_config.sfxEnabled     = settings.GetBool("btn_sfx", true);
        _btn_config.vibrateEnabled = settings.GetBool("btn_vibrate", true);
        mclog::tagInfo(_tag, "config loaded from settings: sfx={}, vibrate={}", _btn_config.sfxEnabled,
                       _btn_config.vibrateEnabled);
    }
    return _btn_config;
}

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "utils/button/Button_Class.hpp"
#include <apps/common/common.h>
#include <memory>
#include <cstdint>
#include <string>
#include <lvgl.h>
#include <functional>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <smooth_lvgl.hpp>
#include <i2c_bus.h>
#include <string_view>
#include <array>
#include <mutex>
#include <M5GFX.h>

/**
 * @brief
 *
 */
struct AlarmStorageEntry {
    uint8_t hour     = 0;
    uint8_t minute   = 0;
    uint8_t enabled  = 0;
    uint8_t reserved = 0;

    bool isValid() const
    {
        return hour < 24 && minute < 60 && enabled <= 1;
    }
};

/**
 * @brief
 *
 */
struct AlarmStorageSnapshot {
    static constexpr std::size_t maxAlarmCount = 16;

    uint8_t version                                     = 1;
    uint8_t count                                       = 0;
    uint16_t reserved                                   = 0;
    std::array<AlarmStorageEntry, maxAlarmCount> alarms = {};
};

/**
 * @brief
 *
 */
struct TimeHms {
    uint8_t hour   = 0;
    uint8_t minute = 0;
    uint8_t second = 0;

    bool isValid() const
    {
        return hour < 24 && minute < 60 && second < 60;
    }
};

/**
 * @brief
 *
 */
struct DateYmd {
    uint16_t year = 2026;
    uint8_t month = 1;
    uint8_t day   = 1;

    static bool isLeapYear(uint16_t year)
    {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    static uint8_t daysInMonth(uint16_t year, uint8_t month)
    {
        static constexpr uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month < 1 || month > 12) {
            return 0;
        }
        if (month == 2 && isLeapYear(year)) {
            return 29;
        }
        return month_days[month - 1];
    }

    bool isValid() const
    {
        const uint8_t max_day = daysInMonth(year, month);
        return year >= 2000 && year <= 2099 && max_day != 0 && day >= 1 && day <= max_day;
    }
};

/**
 * @brief
 *
 */
class BootLogo {
public:
    BootLogo()
    {
        _panel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
        _panel->setSize(466, 466);
        _panel->setAlign(LV_ALIGN_CENTER);
        _panel->setBorderWidth(0);
        _panel->setBgOpa(0);
        _panel->setPaddingAll(0);
        _panel->setBgColor(lv_color_black());

        _label_logo = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
        _label_logo->setTextFont(&lv_font_montserrat_28);
        _label_logo->setTextColor(lv_color_hex(0xFFFFFF));
        _label_logo->align(LV_ALIGN_CENTER, 0, -14);
        _label_logo->setText("StopWatch");

        _label_msg = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
        _label_msg->setTextFont(&lv_font_montserrat_16);
        _label_msg->setTextColor(lv_color_hex(0xBFBFBF));
        _label_msg->align(LV_ALIGN_CENTER, 0, 14);
        _label_msg->setText("Starting up ...");

        _label_version = std::make_unique<uitk::lvgl_cpp::Label>(_panel->get());
        _label_version->setTextFont(&lv_font_montserrat_14);
        _label_version->setTextColor(lv_color_hex(0x8B8B8B));
        _label_version->align(LV_ALIGN_BOTTOM_MID, 0, -12);
        _label_version->setText(common::FirmwareVersion);
    }

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_logo;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_msg;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_version;
};

/**
 * @brief
 *
 */
class Hal {
public:
    void init();

    /* --------------------------------- System --------------------------------- */
    void delay(std::uint32_t ms);
    std::uint32_t millis();
    void feedTheDog();
    std::array<uint8_t, 6> getFactoryMac();
    std::string getFactoryMacString(std::string divider = "");
    void reboot();
    void factoryReset();

    /* ---------------------------------- Power --------------------------------- */
    uint8_t getBatteryLevel();
    bool isBatteryCharging(bool strict = false);
    void setPowerLed(bool enabled);
    void powerOff();

    /* --------------------------------- Display -------------------------------- */
    enum class ColorTemp : uint8_t {
        Cool     = 0,  // 原生冷白
        SoftWarm = 1,  // 柔和暖白
        Warm     = 2,  // 护眼暖色
    };

    void setColorTemperature(ColorTemp temp, bool saveToSettings = false);
    ColorTemp getColorTemperature(bool loadFromSettings = false);
    void setBackLightBrightness(int brightness, bool saveToSettings = false);
    int getBackLightBrightness(bool loadFromSettings = false);
    void setScreenTimeout(uint32_t seconds, bool saveToSettings = true);
    uint32_t getScreenTimeout(bool loadFromSettings = false);
    bool isScreenSleeping() const;
    uint32_t getLastWakeTime() const;
    void sleepScreen();
    void wakeScreen();
    void resetActivityTimer();
    void display_tick();

    // M5GFX
    LGFX_Device& getDisplay();
    LGFX_Sprite& getCanvas();
    void updateCanvas();

    // Lvgl
    lv_indev_t* lvTouchpad = nullptr;
    std::unique_ptr<BootLogo> bootLogo;
    bool lvglLock();
    void lvglUnlock();
    void startLvglUpdate();
    void stopLvglUpdate();

    /* ---------------------------------- Touch --------------------------------- */
    struct TouchPoint {
        int num = 0;
        int x   = -1;
        int y   = -1;
    };
    TouchPoint getTouchPoint();

    /* ---------------------------------- Audio --------------------------------- */
    void setSpeakerVolume(int volume, bool saveToSettings = false);
    int getSpeakerVolume(bool loadFromSettings = false);
    int getAudioSampleRate();
    void audioRecord(std::vector<int16_t>& data, uint16_t durationMs, float gain = 30.0f);
    void audioPlay(std::vector<int16_t>& data, bool async = true);
    bool isAudioPlaying();

    // Streaming audio API for Gemini Live full-duplex / PTT voice interaction
    void setSpeakerEnable(bool enable);
    bool audioRecordStream16k(std::vector<int16_t>& out16k, uint16_t durationMs = 60, float gain = 32.0f);
    void audioPushStreamPcm(const int16_t* samples, std::size_t count, int srcSampleRate = 24000);
    void audioClearStream();
    std::size_t audioGetStreamQueuedMs();
    float audioGetCurrentRms();

    struct AudioSpectrumFrame {
        static constexpr std::size_t bandCount = 20;
        std::array<float, bandCount> bands     = {};
        float peakFrequencyHz                  = 0.0f;
    };
    void updateAudioSpectrum();
    AudioSpectrumFrame getAudioSpectrum() const;

    void playBootSfx();

    /* ----------------------------- Vibrator Motor ----------------------------- */
    void vibrate(uint16_t durationMs, uint8_t strength = 100);
    void stopVibrate();

    /* ----------------------------------- IMU ---------------------------------- */
    struct ImuData {
        float accelX = 0.0f;
        float accelY = 0.0f;
        float accelZ = 0.0f;
        float gyroX  = 0.0f;
        float gyroY  = 0.0f;
        float gyroZ  = 0.0f;
    };
    void updateImuData();
    const ImuData& getImuData() const
    {
        return _imu_data;
    }

    /* ---------------------------------- Time ---------------------------------- */
    void syncRtcTimeToSystem();
    void syncSystemTimeToRtc();
    void sntp_init();
    void sntp_sync();
    DateYmd getDateYmd();
    bool setDateYmd(const DateYmd& date);
    TimeHms getTimeHms();
    bool setTimeHms(const TimeHms& time);
    void setTimezone(std::string_view tz);
    std::string getTimezone();
    bool loadAlarmStorage(AlarmStorageSnapshot& snapshot);
    bool saveAlarmStorage(const AlarmStorageSnapshot& snapshot);
    void startAlarm();
    void stopAlarm();

    /* --------------------------------- Button --------------------------------- */
    m5::Button_Class btnA;
    m5::Button_Class btnB;
    m5::Button_Class btnPwr;

    struct ButtonConfig {
        bool sfxEnabled     = true;
        bool vibrateEnabled = true;
    };

    void updateButtonStates();
    void pwr_button_tick();
    void setButtonConfig(ButtonConfig config, bool saveToSettings = false);
    const ButtonConfig& getButtonConfig(bool loadFromSettings = false);

    /* ---------------------------------- Wi-Fi --------------------------------- */
    enum class WifiMode {
        Off,
        Connecting,
        Connected,
        ConfigAp,
        BadgeAp,
        Error,
    };

    struct WifiStatus {
        bool enabled    = false;
        WifiMode mode   = WifiMode::Off;
        bool connected  = false;
        bool configMode = false;
        bool badgeMode  = false;
        std::string ssid;
        std::string ipAddress;
        std::string apSsid;
        std::string apUrl;
        std::string message;
    };

    void setWifiEnabled(bool enabled, bool saveToSettings = true);
    bool isWifiEnabled(bool loadFromSettings = false);
    bool isWifiConnected();
    WifiStatus getWifiStatus();
    void wifi_enter_badge_ap();
    void wifi_exit_badge_ap();
    void wifi_on_screen_wake();

    /* ---------------------------------- Badge --------------------------------- */
    bool loadBadgeImage(lv_obj_t* image);
    bool loadNextBadgeImage(lv_obj_t* image);
    bool loadPreviousBadgeImage(lv_obj_t* image);
    bool loadNextActiveBadgeImage(lv_obj_t* image);
    uint32_t getBadgeCarouselIntervalMs();
    void setBadgeCarouselIntervalSeconds(uint32_t seconds);
    bool isBadgeSlotActive(std::size_t slot);
    bool setBadgeSlotActive(std::size_t slot, bool active);
    bool startBadgeEditModeViaAp(std::function<void(std::string_view)> onLog);

    /* ---------------------------------- Guide --------------------------------- */
    bool shouldShowGuide();

    /* ----------------------------------- OTA ---------------------------------- */
    enum class OtaState {
        Idle,
        Ready,
        Flashing,
        Success,
        Failed
    };

    struct OtaStatus {
        OtaState state = OtaState::Idle;
        int progress   = 0;
        std::size_t bytesReceived = 0;
        std::size_t totalBytes    = 0;
        std::string runningPartition;
        std::string targetPartition;
        std::string ipAddress;
        std::string ssid;
        std::string message;
    };

    bool startOta(std::function<void(std::string_view)> onLog = nullptr);
    void stopOta();
    bool isOtaRunning();
    OtaStatus getOtaStatus();

    /* -------------------------------- Eye Care -------------------------------- */
    void setEyeCareReminder(bool enabled, bool saveToSettings = true);
    bool getEyeCareReminder(bool loadFromSettings = false);
    void eye_care_tick();
    void playEyeCareDing();

private:
    static constexpr std::string_view SettingsNs = "system";

    i2c_bus_handle_t _i2c_bus = nullptr;
    ImuData _imu_data;
    ButtonConfig _btn_config;
    int _bl_brightness = 80;
    int _spk_volume    = 80;

    void i2c_init();
    void i2c_detect();
    void pmic_init();
    bool pmic_get_pwr_btn_state();
    bool pmic_get_double_click_flag();
    void ioe_init();
    void ioe_tp_reset();
    void ioe_speaker_enable(bool enable);
    void display_init();
    void touchpad_init();
    void lvgl_init();
    void audio_init();
    void imu_init();
    void rtc_init();
    void button_init();
    void fs_init();
    void wifi_init();
    void wifi_tick();
};

Hal& GetHAL();

/**
 * @brief
 *
 */
class LvglLockGuard {
public:
    LvglLockGuard()
    {
        GetHAL().lvglLock();
    }
    ~LvglLockGuard()
    {
        GetHAL().lvglUnlock();
    }
};

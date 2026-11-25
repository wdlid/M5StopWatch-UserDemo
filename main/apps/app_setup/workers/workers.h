/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <hal/hal.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace setup_workers {

class PercentageAdjustView;

/**
 * @brief
 *
 */
class WorkerBase {
public:
    virtual ~WorkerBase() = default;

    virtual void update()
    {
    }

    bool isDone() const
    {
        return _is_done;
    }

protected:
    bool _is_done = false;
};

/**
 * @brief
 *
 */
class BrightnessWorker : public WorkerBase {
public:
    BrightnessWorker();
    ~BrightnessWorker();
    void update() override;

private:
    std::unique_ptr<PercentageAdjustView> _view;
    int _applied_brightness = 0;
    int _initial_brightness = 0;
    bool _saved             = false;
};

/**
 * @brief
 *
 */
class ScreenTimeoutWorker : public WorkerBase {
public:
    ScreenTimeoutWorker();
    ~ScreenTimeoutWorker();
    void update() override;

private:
    class ScreenTimeoutView;
    std::unique_ptr<ScreenTimeoutView> _view;
    uint32_t _applied_timeout = 30;
};

/**
 * @brief
 *
 */
class ColorTempWorker : public WorkerBase {
public:
    ColorTempWorker();
    ~ColorTempWorker();
    void update() override;

private:
    class ColorTempView;
    std::unique_ptr<ColorTempView> _view;
    Hal::ColorTemp _applied_temp = Hal::ColorTemp::Cool;
    Hal::ColorTemp _initial_temp = Hal::ColorTemp::Cool;
    bool _saved                  = false;
};

/**
 * @brief
 *
 */
class VolumeWorker : public WorkerBase {
public:
    VolumeWorker();
    ~VolumeWorker();
    void update() override;

private:
    std::unique_ptr<PercentageAdjustView> _view;
    int _applied_volume = 0;
    int _initial_volume = 0;
    bool _saved         = false;
};

/**
 * @brief
 *
 */
class ButtonWorker : public WorkerBase {
public:
    ButtonWorker();
    ~ButtonWorker();
    void update() override;

private:
    class ButtonConfigView;

    std::unique_ptr<ButtonConfigView> _view;
    Hal::ButtonConfig _applied_config;
    Hal::ButtonConfig _initial_config;
    bool _saved = false;
};

/**
 * @brief
 *
 */
class EyeCareWorker : public WorkerBase {
public:
    EyeCareWorker();
    ~EyeCareWorker();
    void update() override;

private:
    class EyeCareView;

    std::unique_ptr<EyeCareView> _view;
    bool _applied_enabled = false;
    bool _initial_enabled = false;
    bool _saved           = false;
};

/**
 * @brief
 *
 */
class WifiWorker : public WorkerBase {
public:
    WifiWorker();
    ~WifiWorker();
    void update() override;

private:
    class WifiConfigView;

    std::unique_ptr<WifiConfigView> _view;
    Hal::WifiStatus _last_status;
};

/**
 * @brief
 *
 */
class SetTimeWorker : public WorkerBase {
public:
    SetTimeWorker();
    ~SetTimeWorker();
    void update() override;

private:
    class TimeAdjustView;

    std::unique_ptr<TimeAdjustView> _view;
    TimeHms _applied_time;
};

/**
 * @brief
 *
 */
class SetDateWorker : public WorkerBase {
public:
    SetDateWorker();
    ~SetDateWorker();
    void update() override;

private:
    class DateAdjustView;

    std::unique_ptr<DateAdjustView> _view;
    DateYmd _applied_date;
};

/**
 * @brief
 *
 */
class TimezoneWorker : public WorkerBase {
public:
    TimezoneWorker();
    ~TimezoneWorker();
    void update() override;

private:
    class TimezoneView;

    std::unique_ptr<TimezoneView> _view;
    std::string _applied_tz;
};

/**
 * @brief
 *
 */
class AboutWorker : public WorkerBase {
public:
    AboutWorker();
    ~AboutWorker();
    void update() override;

private:
    class AboutView;

    std::unique_ptr<AboutView> _view;
    int _progress                = 0;
    uint32_t _next_progress_tick = 0;
    int _pending_burst_steps     = 0;
};

/**
 * @brief
 *
 */
class OtaWorker : public WorkerBase {
public:
    OtaWorker();
    ~OtaWorker();
    void update() override;

private:
    class OtaView;
    std::unique_ptr<OtaView> _view;
};

/**
 * @brief Worker to check latest release from GitHub and perform OTA update
 *
 */
class CheckUpdateWorker : public WorkerBase {
public:
    CheckUpdateWorker();
    ~CheckUpdateWorker();
    void update() override;

private:
    class CheckUpdateView;
    std::unique_ptr<CheckUpdateView> _view;

    enum class Step {
        None,
        CheckingWifi,
        CheckingGithub,
        WifiNotConnected,
        CheckFailed,
        UpToDate,
        UpdateAvailable,
        Downloading,
        UpdateSuccess,
        UpdateFailed
    };

    struct UpdateInfo {
        std::string latestVersion;
        std::string downloadUrl;
        size_t fileSize      = 0;
        std::string errorMsg;
        int progress         = 0;
        size_t bytesReceived = 0;
        size_t totalBytes    = 0;
    };

    struct CheckTaskContext;
    struct OtaTaskContext;

    Step _step               = Step::CheckingWifi;
    Step _last_rendered_step = Step::None;
    UpdateInfo _info;

    std::shared_ptr<CheckTaskContext> _check_ctx;
    std::shared_ptr<OtaTaskContext> _ota_ctx;

    uint32_t _created_at       = 0;
    uint32_t _check_started_at = 0;
    uint32_t _reboot_at        = 0;

    void _startCheckTask();
    void _startOtaTask();
    void _stopTask();
};

}  // namespace setup_workers

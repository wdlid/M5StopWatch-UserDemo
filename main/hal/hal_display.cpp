/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "drivers/cst820/cst820.h"
#include "utils/settings/settings.h"
#include <mooncake_log.h>
#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_AMOLED.hpp>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <memory>
#include <atomic>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

static const std::string_view _tag = "HAL-Display";

static SemaphoreHandle_t xGuiSemaphore;
static std::atomic<bool> _lvgl_update_enabled = false;

namespace {

// Drain in-flight LVGL flushes before touching the panel. If the caller already
// owns the LVGL mutex, skip re-locking (non-recursive) to avoid self-deadlock.
class LvglDrainGuard {
public:
    LvglDrainGuard()
    {
        if (xGuiSemaphore == nullptr) {
            return;
        }
        _locked_here = xSemaphoreGetMutexHolder(xGuiSemaphore) != xTaskGetCurrentTaskHandle();
        if (_locked_here) {
            GetHAL().lvglLock();
        }
    }

    ~LvglDrainGuard()
    {
        if (_locked_here) {
            GetHAL().lvglUnlock();
        }
    }

private:
    bool _locked_here = false;
};

}  // namespace

/* -------------------------------------------------------------------------- */
/*                               Amoled display                               */
/* -------------------------------------------------------------------------- */
static constexpr gpio_num_t cfg_pin_sclk = GPIO_NUM_40;
static constexpr gpio_num_t cfg_pin_io0  = GPIO_NUM_41;
static constexpr gpio_num_t cfg_pin_io1  = GPIO_NUM_42;
static constexpr gpio_num_t cfg_pin_io2  = GPIO_NUM_46;
static constexpr gpio_num_t cfg_pin_io3  = GPIO_NUM_45;
static constexpr gpio_num_t cfg_pin_cs   = GPIO_NUM_39;
static constexpr gpio_num_t cfg_pin_te   = GPIO_NUM_38;
static constexpr gpio_num_t cfg_pin_rst  = GPIO_NUM_NC;

class Panel_CO5300 : public lgfx::Panel_AMOLED {
public:
    Panel_CO5300(void)
    {
        _cfg.memory_width = _cfg.panel_width = 480;
        _cfg.memory_height = _cfg.panel_height = 480;
        _write_depth                           = lgfx::color_depth_t::rgb565_2Byte;
        _read_depth                            = lgfx::color_depth_t::rgb565_2Byte;
    }

    const uint8_t *getInitCommands(uint8_t listno) const override
    {
        // Keep WRCTRL/brightness at 0 through Display ON so uninitialized GRAM
        // is never illuminated (avoids the white flash on cold boot / deep-sleep wake).
        static constexpr uint8_t list0[] = {
            0x11, 0 + CMD_INIT_DELAY,
            150,  // Sleep out
            0xC4, 1,
            0x80, 0x35,
            1,    0x80,
            0x44, 2,
            0x01, 0xD2,  // Tear Effect Line = 0x1D2 == 466
            0x53, 1,
            0x20, 0x20,
            0,    0x36,
            1,    0,
            0x51, 1,
            0x00, 0x29,  // brightness 0, then Display ON
            0,    0xff,
            0xff  // end
        };
        switch (listno) {
            case 0:
                return list0;
            default:
                return nullptr;
        }
    }
};

class M5StopWatch : public M5GFX {
    lgfx::Bus_SPI _bus_instance;
    Panel_CO5300 _panel_instance;

public:
    M5StopWatch(void)
    {
    }

    // static constexpr int in_i2c_port                   = 0;  // I2C_NUM_0

    bool init_impl(bool use_reset, bool use_clear) override
    {
        {
            auto cfg = _bus_instance.config();

            cfg.freq_write = 80000000;
            cfg.freq_read  = 10000000;  // irrelevant

            cfg.pin_sclk = cfg_pin_sclk;
            cfg.pin_io0  = cfg_pin_io0;
            cfg.pin_io1  = cfg_pin_io1;
            cfg.pin_io2  = cfg_pin_io2;
            cfg.pin_io3  = cfg_pin_io3;

            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;  // SPI_MODE0;
            cfg.spi_3wire   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg         = _panel_instance.config();
            cfg.pin_rst      = cfg_pin_rst;
            cfg.pin_cs       = cfg_pin_cs;
            cfg.panel_width  = 468;
            cfg.panel_height = 466;
            cfg.offset_x     = 6;
            cfg.offset_y     = 0;

            cfg.readable = false;

            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);

        lgfx::pinMode(cfg_pin_te, lgfx::pin_mode_t::input_pullup);
        // lgfx::i2c::init(in_i2c_port);

        // io_expander.digitalWrite(PY32_L3B_EN_PIN, 1);
        // io_expander.digitalWrite(PY32_OLED_RST_PIN, 1);

        // So init_impl's post-clear setBrightness() stays dark (default is 127).
        _brightness = 0;

        if (!LGFX_Device::init_impl(use_reset, use_clear)) return false;

        enableFrameBuffer(false);

        // Zero the FB in RAM only. Do NOT call display() here: a full-screen push
        // without TE sync desynchronizes this panel (content shifts, e.g. right
        // quarter wrapping to the left). LVGL's flush path waits for TE first.
        clear();
        _panel_instance.setBrightness(0);

        return true;
    }

    bool enableFrameBuffer(bool auto_display = false)
    {
        if (_panel_instance.initPanelFb()) {
            auto fbPanel = _panel_instance.getPanelFb();
            if (fbPanel) {
                fbPanel->setBus(&_bus_instance);
                fbPanel->setAutoDisplay(auto_display);
                setPanel(fbPanel);
                return true;
            }
        }
        return false;
    }

    void disableFrameBuffer()
    {
        auto fbPanel = _panel_instance.getPanelFb();
        if (fbPanel) {
            _panel_instance.deinitPanelFb();
            setPanel(&_panel_instance);
        }
    }

    void setBrightness(uint8_t brightness)
    {
        _panel_instance.setBrightness(brightness);
    }

    void setSleep(bool sleep)
    {
        _panel_instance.startWrite();
        _panel_instance.setSleep(sleep);
        _panel_instance.endWrite();
    }
};

static std::unique_ptr<M5StopWatch> _display;
static std::unique_ptr<LGFX_Sprite> _canvas;
static std::unique_ptr<Cst820> _cst820;

static bool _screen_sleeping           = false;
static uint32_t _screen_timeout_sec    = 30;
static uint32_t _last_activity_time    = 0;
static uint32_t _last_wake_time        = 0;
static uint32_t _screen_sleep_start_ms = 0;

// Soft-standby window after screen sleep before entering deep sleep.
static constexpr uint32_t _deep_sleep_after_ms = 30000;

static std::atomic<Hal::ColorTemp> _color_temp = Hal::ColorTemp::Cool;
static uint16_t _lut_r[32];
static uint16_t _lut_g[64];
static uint16_t _lut_b[32];

static void update_color_temp_lut(Hal::ColorTemp temp)
{
    if (temp == Hal::ColorTemp::Cool) {
        _color_temp.store(temp, std::memory_order_release);
        return;
    }

    // R: 5 bits (0..31), bit 11..15
    for (int i = 0; i < 32; ++i) {
        _lut_r[i] = (uint16_t)(i << 11);
    }

    if (temp == Hal::ColorTemp::SoftWarm) {
        // Soft Warm: G roughly 98% (63 -> 62)
        for (int i = 0; i < 64; ++i) {
            uint16_t g = (uint16_t)((i * 62 + 31) / 63);
            _lut_g[i]  = (uint16_t)(g << 5);
        }
        // Soft Warm: B roughly 87% (31 -> 27)
        for (int i = 0; i < 32; ++i) {
            uint16_t b = (uint16_t)((i * 27 + 15) / 31);
            _lut_b[i]  = b;
        }
    } else {  // Warm
        // Warm: G roughly 94% (63 -> 59)
        for (int i = 0; i < 64; ++i) {
            uint16_t g = (uint16_t)((i * 59 + 31) / 63);
            _lut_g[i]  = (uint16_t)(g << 5);
        }
        // Warm: B roughly 74% (31 -> 23)
        for (int i = 0; i < 32; ++i) {
            uint16_t b = (uint16_t)((i * 23 + 15) / 31);
            _lut_b[i]  = b;
        }
    }

    _color_temp.store(temp, std::memory_order_release);
}

void Hal::setColorTemperature(ColorTemp temp, bool saveToSettings)
{
    update_color_temp_lut(temp);

    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("color_temp", static_cast<int>(temp));
        mclog::tagInfo(_tag, "color temp saved to settings: {}", static_cast<int>(temp));
    }
}

Hal::ColorTemp Hal::getColorTemperature(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        int temp_val = settings.GetInt("color_temp", 0);
        if (temp_val < 0 || temp_val > 2) {
            temp_val = 0;
        }
        update_color_temp_lut(static_cast<ColorTemp>(temp_val));
        mclog::tagInfo(_tag, "color temp loaded from settings: {}", temp_val);
    }
    return _color_temp.load(std::memory_order_acquire);
}

/* ---------------------------------- Theme --------------------------------- */
static const Hal::GlobalTheme _theme_dark = {
    .background         = 0x000000,
    .cardBackground     = 0x1B1F24,
    .cardPressed        = 0x2A3038,
    .textPrimary        = 0xFFFFFF,
    .textSecondary      = 0x8FA0B0,
    .textMuted          = 0x5A6570,
    .border             = 0x2A3038,
    .accent             = 0x4AD78C,
    .statusBarPrimary   = 0x385179,
    .statusBarSecondary = 0xEDF4FF,
};

static const Hal::GlobalTheme _theme_light = {
    .background         = 0xF7E9B8,  // Warm yellow paper (Claude-style paper)
    .cardBackground     = 0xFCF4D4,  // Soft warm ivory-yellow card
    .cardPressed        = 0xEDE0AC,  // Warm pressed yellow-tan feedback
    .textPrimary        = 0x1F1C14,  // Deep warm charcoal ink
    .textSecondary      = 0x6B624C,  // Warm olive-taupe secondary text
    .textMuted          = 0x9B9076,  // Muted warm sepia-gray
    .border             = 0xE4D39B,  // Subtle golden-tan border
    .accent             = 0xD97757,  // Anthropic terracotta accent
    .statusBarPrimary   = 0x2B261B,  // Warm dark ink status bar text/icons
    .statusBarSecondary = 0xF7E9B8,  // Matches warm yellow paper background
};

static std::atomic<Hal::ThemeMode> _theme_mode{Hal::ThemeMode::Light};
static std::atomic<int> _clock_face_index{0};

void Hal::setClockFaceIndex(int index, bool saveToSettings)
{
    if (index < 0) {
        index = 0;
    }
    _clock_face_index.store(index, std::memory_order_release);
    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("clock_face", index);
        mclog::tagInfo(_tag, "clock face saved to settings: {}", index);
    }
}

int Hal::getClockFaceIndex(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        int val = settings.GetInt("clock_face", 0);
        if (val < 0) {
            val = 0;
        }
        _clock_face_index.store(val, std::memory_order_release);
        mclog::tagInfo(_tag, "clock face loaded from settings: {}", val);
    }
    return _clock_face_index.load(std::memory_order_acquire);
}

void Hal::setThemeMode(Hal::ThemeMode mode, bool saveToSettings)
{
    _theme_mode.store(mode, std::memory_order_release);
    mclog::tagInfo(_tag, "set theme mode: {}", mode == ThemeMode::Dark ? "Dark" : "Light");

    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("theme_mode", static_cast<int>(mode));
        mclog::tagInfo(_tag, "theme mode saved to settings: {}", static_cast<int>(mode));
    }

    // Caller must already hold the LVGL lock (non-recursive mutex).
    if (lv_is_initialized()) {
        uitk::lvgl_cpp::ScreenActive screen;
        screen.setBgColor(lv_color_hex(getTheme().background));
        lv_obj_invalidate(lv_screen_active());
    }
}

Hal::ThemeMode Hal::getThemeMode(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        int val = settings.GetInt("theme_mode", 1);
        if (val < 0 || val > 1) {
            val = 0;
        }
        _theme_mode.store(static_cast<ThemeMode>(val), std::memory_order_release);
        mclog::tagInfo(_tag, "theme mode loaded from settings: {}", val);
    }
    return _theme_mode.load(std::memory_order_acquire);
}

const Hal::GlobalTheme& Hal::getTheme() const
{
    return (_theme_mode.load(std::memory_order_acquire) == ThemeMode::Light) ? _theme_light : _theme_dark;
}

void Hal::display_init()
{
    mclog::tagInfo(_tag, "display init");

    // The AMOLED itself is the primary visible power indication. Keep the
    // separate PMIC power LED dark whenever the display is about to be used.
    setPowerLed(false);

    _display = std::make_unique<M5StopWatch>();
    if (!_display->init()) {
        mclog::tagError(_tag, "display init failed");
        _display.reset();
    }

    // mclog::tagInfo(_tag, "create full screen canvas");
    // _canvas = std::make_unique<LGFX_Sprite>(_display.get());
    // _canvas->setPsram(true);
    // if (!_canvas->createSprite(_display->width(), _display->height())) {
    //     mclog::tagError(_tag, "canvas init failed");
    //     _canvas.reset();
    // }

    // Load brightness from settings but keep the panel dark until LVGL has
    // flushed the first frame (see lvgl_init). Applying brightness here is what
    // made deep-sleep wake flash an empty white GRAM before the UI appeared.
    getBackLightBrightness(true);
    if (_display) {
        _display->setBrightness(0);
    }

    // Load color temperature from settings
    getColorTemperature(true);

    // Load theme from settings
    getThemeMode(true);

    // Load preferred clock face from settings
    getClockFaceIndex(true);

    getScreenTimeout(true);
    _last_activity_time = millis();
}

LGFX_Device &Hal::getDisplay()
{
    return *_display;
}

LGFX_Sprite &Hal::getCanvas()
{
    return *_canvas;
}

void Hal::updateCanvas()
{
    _canvas->pushSprite(0, 0);
}

void Hal::setBackLightBrightness(int brightness, bool saveToSettings)
{
    _bl_brightness = uitk::clamp(brightness, 0, 100);

    if (!_screen_sleeping) {
        int set_target = uitk::map_range(_bl_brightness, 0, 100, 0, 255);

        // Never illuminate the redundant power LED while the AMOLED is on. Turn
        // it back on only when brightness is explicitly set to zero.
        if (_bl_brightness > 0) {
            setPowerLed(false);
        }
        _display->setBrightness(set_target);
        if (_bl_brightness == 0) {
            setPowerLed(true);
        }
    }

    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("bl_lev", _bl_brightness);
        mclog::tagInfo(_tag, "brightness saved to settings: {}", _bl_brightness);
    }
}

int Hal::getBackLightBrightness(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _bl_brightness = settings.GetInt("bl_lev", 80);
        _bl_brightness = uitk::clamp(_bl_brightness, 10, 100);
        mclog::tagInfo(_tag, "brightness loaded from settings: {}", _bl_brightness);
    }
    return _bl_brightness;
}

void Hal::setScreenTimeout(uint32_t seconds, bool saveToSettings)
{
    _screen_timeout_sec = seconds;
    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("scr_timeout", static_cast<int>(seconds));
        mclog::tagInfo(_tag, "screen timeout saved: {} s", seconds);
    }
}

uint32_t Hal::getScreenTimeout(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _screen_timeout_sec = static_cast<uint32_t>(settings.GetInt("scr_timeout", 30));
        mclog::tagInfo(_tag, "screen timeout loaded: {} s", _screen_timeout_sec);
    }
    return _screen_timeout_sec;
}

bool Hal::isScreenSleeping() const
{
    return _screen_sleeping;
}

uint32_t Hal::getLastWakeTime() const
{
    return _last_wake_time;
}

void Hal::sleepScreen()
{
    if (_screen_sleeping) {
        return;
    }
    _screen_sleeping       = true;
    _screen_sleep_start_ms = millis();
    stopLvglUpdate();
    {
        // Wait out any concurrent lv_timer_handler/flush before sleeping the
        // panel; racing SPI with OLED power-down can freeze the UI while the
        // green power LED keeps running on PMIC/USB power.
        LvglDrainGuard drain;
        if (_display) {
            _display->setBrightness(0);
            _display->setSleep(true);
        }
    }
    if (_cst820) {
        _cst820->sleep();
    }
    if (!isAudioPlaying()) {
        audio_enter_standby();
    }
    setPowerLed(false);
    button_on_sleep();
    mclog::tagInfo(_tag, "screen sleep (deep sleep in {}s if idle)", _deep_sleep_after_ms / 1000);
}

void Hal::wakeScreen()
{
    if (!_screen_sleeping) {
        return;
    }
    _screen_sleeping       = false;
    _screen_sleep_start_ms = 0;
    _last_activity_time    = millis();
    _last_wake_time        = _last_activity_time;
    ioe_tp_reset();
    if (_display) {
        _display->setSleep(false);
        int set_target = uitk::map_range(_bl_brightness, 0, 100, 0, 255);
        _display->setBrightness(set_target);
    }
    audio_exit_standby();
    setPowerLed(false);
    startLvglUpdate();
    mclog::tagInfo(_tag, "screen wake");

    wifi_on_screen_wake();
}

void Hal::resetActivityTimer()
{
    _last_activity_time = millis();
}

void Hal::display_tick()
{
    if (_screen_sleeping) {
        if (isOtaRunning() || isAudioPlaying()) {
            return;
        }
        const auto wifi = getWifiStatus();
        if (wifi.badgeMode || wifi.configMode) {
            return;
        }
        if (_screen_sleep_start_ms != 0 &&
            millis() - _screen_sleep_start_ms >= _deep_sleep_after_ms) {
            enterDeepSleepStandby();
            // Returned => deferred (e.g. alarm soon). Retry after another soft-standby window.
            _screen_sleep_start_ms = millis();
        }
        return;
    }

    if (_screen_timeout_sec == 0) {
        return;
    }
    if (millis() - _last_activity_time >= _screen_timeout_sec * 1000) {
        if (!isOtaRunning()) {
            sleepScreen();
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                                  Touchpad                                  */
/* -------------------------------------------------------------------------- */

void Hal::touchpad_init()
{
    mclog::tagInfo(_tag, "touchpad init");

    ioe_tp_reset();

    _cst820 = std::make_unique<Cst820>();
    if (!_cst820->begin(i2c_bus_get_internal_bus_handle(_i2c_bus))) {
        mclog::tagError(_tag, "touchpad init failed");
        _cst820.reset();
    }
}

Hal::TouchPoint Hal::getTouchPoint()
{
    static Hal::TouchPoint _last_valid_point;
    static uint8_t _i2c_fail_streak = 0;

    if (!_cst820) {
        return {};
    }
    if (_cst820->read()) {
        _i2c_fail_streak = 0;
        Hal::TouchPoint point;
        point.num = _cst820->getFingerNum();
        if (point.num > 0) {
            point.x = _cst820->getX();
            point.y = _cst820->getY();
        }
        _last_valid_point = point;
        return point;
    }

    // Hold last valid touch state across brief I2C bus contention so LVGL
    // never sees a false finger release (LV_INDEV_STATE_REL) mid-swipe.
    if (++_i2c_fail_streak <= 3) {
        return _last_valid_point;
    }
    _last_valid_point = {};
    return {};
}

/* -------------------------------------------------------------------------- */
/*                                    Lvgl                                    */
/* -------------------------------------------------------------------------- */
// https://github.com/m5stack/lv_m5_emulator/blob/main/src/utility/lvgl_port_m5stack.cpp
#include <cstdlib>  // for aligned_alloc
#include <cstring>  // for memset
#include <lvgl.h>

// ~96 lines ≈ 20% of 466px / ~87 KB in SPIRAM. Below ~10% (e.g. 32 lines) the
// partial-tile count dominates; 10–25% is the Espressif/Seeed sweet spot. Keep
// the buffer in PSRAM so internal SRAM stays free for Wi-Fi / mbedTLS / sync objs.
#define LV_BUFFER_LINE 96

static uint32_t lvgl_tick_get_cb(void)
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

static void lvgl_rtos_task(void *pvParameter)
{
    (void)pvParameter;
    while (1) {
        if (!_lvgl_update_enabled) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        uint32_t time_till_next = 5;
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) {
            // Recheck after taking the mutex. stopLvglUpdate() may have been
            // called while this task was waiting for an in-flight UI update.
            if (_lvgl_update_enabled) {
                time_till_next = lv_timer_handler();
            }
            xSemaphoreGive(xGuiSemaphore);
        }
        uint32_t delay_ms = uitk::clamp(time_till_next, 1U, 10U);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static inline void wait_for_te()
{
    static int timeout_count = 0;
    if (timeout_count > 5) {
        return;
    }
    uint32_t start = esp_timer_get_time();
    // Wait for TE rising edge (or active high period) with bounded timeout
    while (gpio_get_level(cfg_pin_te) == 0) {
        if ((uint32_t)(esp_timer_get_time() - start) > 12000) {
            timeout_count++;
            return;
        }
    }
    timeout_count = 0;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    M5GFX &gfx = *(M5GFX *)lv_display_get_driver_data(disp);

    uint32_t w      = (area->x2 - area->x1 + 1);
    uint32_t h      = (area->y2 - area->y1 + 1);
    uint32_t pixels = w * h;

    auto cur_temp = _color_temp.load(std::memory_order_acquire);
    if (cur_temp != Hal::ColorTemp::Cool) {
        uint16_t *buf = (uint16_t *)px_map;
        for (uint32_t i = 0; i < pixels; ++i) {
            uint16_t c = buf[i];
            buf[i]     = _lut_r[(c >> 11) & 0x1F] | _lut_g[(c >> 5) & 0x3F] | _lut_b[c & 0x1F];
        }
    }

    // Fast 32-bit in-place byte swap so M5GFX can memcpy rows directly into its framebuffer
    lv_draw_sw_rgb565_swap(px_map, pixels);

    gfx.startWrite();
    gfx.setAddrWindow(area->x1, area->y1, w, h);
    gfx.writePixels((const lgfx::swap565_t *)px_map, pixels);
    gfx.endWrite();

    if (lv_display_flush_is_last(disp)) {
        wait_for_te();
        gfx.display();
    }

    lv_display_flush_ready(disp);
}

static void lvgl_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (GetHAL().isScreenSleeping()) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    auto tp = GetHAL().getTouchPoint();
    if (tp.num == 0) {
        data->state = LV_INDEV_STATE_REL;
    } else {
        GetHAL().resetActivityTimer();
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = tp.x;
        data->point.y = tp.y;
    }
}

void Hal::lvgl_init()
{
    mclog::tagInfo(_tag, "lvgl init");

    lv_init();

    static lv_display_t *disp = lv_display_create(_display->width(), _display->height());
    if (disp == NULL) {
        printf("lv_display_create failed\n");
        return;
    }

    lv_display_set_driver_data(disp, _display.get());
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    // Single SPIRAM partial buffer. Flush is synchronous, so a 2nd buffer adds no
    // DMA overlap; stay in PSRAM to avoid starving Wi-Fi / mbedTLS internal heap.
    const size_t buf_size = _display->width() * LV_BUFFER_LINE * sizeof(lv_color_t);
    static uint8_t *buf1  = (uint8_t *)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (buf1 == nullptr) {
        mclog::tagError(_tag, "lvgl draw buf alloc failed ({} B)", buf_size);
        return;
    }
    mclog::tagInfo(_tag, "lvgl draw buf in SPIRAM ({} lines, {} B)", LV_BUFFER_LINE, buf_size);
    lv_display_set_buffers(disp, (void *)buf1, nullptr, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lvTouchpad = lv_indev_create();
    LV_ASSERT_MALLOC(lvTouchpad);
    if (lvTouchpad == NULL) {
        printf("lv_indev_create failed\n");
        return;
    }
    lv_indev_set_driver_data(lvTouchpad, _display.get());
    lv_indev_set_type(lvTouchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvTouchpad, lvgl_read_cb);
    lv_indev_set_display(lvTouchpad, disp);
    lv_indev_set_scroll_limit(lvTouchpad, 14);
    lv_indev_set_scroll_throw(lvTouchpad, 7);
    if (lv_timer_t *indev_timer = lv_indev_get_read_timer(lvTouchpad)) {
        lv_timer_set_period(indev_timer, 8);
    }

    xGuiSemaphore = xSemaphoreCreateMutex();
    lv_tick_set_cb(lvgl_tick_get_cb);
    xTaskCreatePinnedToCore(lvgl_rtos_task, "lvgl_rtos_task", 4096 * 4, NULL, 5, NULL, 1);

    startLvglUpdate();

    {
        LvglLockGuard lock;
        uitk::lvgl_cpp::ScreenActive screen;
        screen.setBgColor(lv_color_hex(getTheme().background));
        GetHAL().bootLogo = std::make_unique<BootLogo>();
        // Paint the boot frame while brightness is still 0, then illuminate.
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(nullptr);
    }

    setBackLightBrightness(_bl_brightness, false);
}

bool Hal::lvglLock()
{
    return xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) == pdTRUE ? true : false;
}

void Hal::lvglUnlock()
{
    xSemaphoreGive(xGuiSemaphore);
}

void Hal::startLvglUpdate()
{
    _lvgl_update_enabled = true;
}

void Hal::stopLvglUpdate()
{
    _lvgl_update_enabled = false;
}

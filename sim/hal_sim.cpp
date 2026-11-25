#include <hal/hal.h>
#include <mooncake_log.h>
#include <SDL.h>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <emscripten.h>

static bool _sim_btn_raw[3] = {false, false, false};

extern "C" {
EMSCRIPTEN_KEEPALIVE void sim_set_button(int idx, int pressed)
{
    if (idx >= 0 && idx < 3) {
        _sim_btn_raw[idx] = (pressed != 0);
    }
}
}

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
    .background         = 0xF7E9B8,
    .cardBackground     = 0xFCF4D4,
    .cardPressed        = 0xEDE0AC,
    .textPrimary        = 0x1F1C14,
    .textSecondary      = 0x6B624C,
    .textMuted          = 0x9B9076,
    .border             = 0xE4D39B,
    .accent             = 0xD97757,
    .statusBarPrimary   = 0x2B261B,
    .statusBarSecondary = 0xF7E9B8,
};

static Hal::ThemeMode _sim_theme_mode = Hal::ThemeMode::Light;
static int _sim_clock_face_index      = 0;
static Hal::ColorTemp _sim_color_temp = Hal::ColorTemp::Cool;
static uint32_t _sim_screen_timeout   = 30;
static std::string _sim_timezone      = "UTC0";
static bool _sim_wifi_enabled         = true;
static bool _sim_eyecare              = false;
static AlarmStorageSnapshot _sim_alarms;
static LGFX_Device _sim_display;
static LGFX_Sprite _sim_canvas;
static Hal::AudioSpectrumFrame _sim_spectrum;
static Hal _hal_instance;

Hal& GetHAL()
{
    return _hal_instance;
}

void Hal::init()
{
    mclog::tagInfo("HAL-Sim", "WebAssembly LVGL Simulator initialized (466x466)");
}

void Hal::delay(std::uint32_t ms)
{
    SDL_Delay(ms);
}

std::uint32_t Hal::millis()
{
    return static_cast<std::uint32_t>(SDL_GetTicks());
}

void Hal::feedTheDog() {}

std::array<uint8_t, 6> Hal::getFactoryMac()
{
    return {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
}

std::string Hal::getFactoryMacString(std::string divider)
{
    return "AA" + divider + "BB" + divider + "CC" + divider + "DD" + divider + "EE" + divider + "FF";
}

void Hal::reboot() {}
void Hal::factoryReset() {}

uint8_t Hal::getBatteryLevel() { return 88; }
bool Hal::isBatteryCharging(bool) { return false; }
void Hal::setPowerLed(bool) {}
void Hal::requestPowerOff() { _poweroff_requested.store(true); }
bool Hal::isPowerOffRequested() const { return _poweroff_requested.load(); }
void Hal::powerOff() {}
bool Hal::wokeFromDeepSleepStandby() const { return false; }
void Hal::audio_enter_standby() {}
void Hal::audio_exit_standby() {}

void Hal::setThemeMode(ThemeMode mode, bool) { _sim_theme_mode = mode; }
Hal::ThemeMode Hal::getThemeMode(bool) { return _sim_theme_mode; }
const Hal::GlobalTheme& Hal::getTheme() const
{
    return (_sim_theme_mode == ThemeMode::Light) ? _theme_light : _theme_dark;
}
void Hal::setClockFaceIndex(int index, bool) { _sim_clock_face_index = std::max(0, index); }
int Hal::getClockFaceIndex(bool) { return _sim_clock_face_index; }

void Hal::setColorTemperature(ColorTemp temp, bool) { _sim_color_temp = temp; }
Hal::ColorTemp Hal::getColorTemperature(bool) { return _sim_color_temp; }
void Hal::setBackLightBrightness(int brightness, bool) { _bl_brightness = std::clamp(brightness, 0, 100); }
int Hal::getBackLightBrightness(bool) { return _bl_brightness; }
void Hal::setScreenTimeout(uint32_t seconds, bool) { _sim_screen_timeout = seconds; }
uint32_t Hal::getScreenTimeout(bool) { return _sim_screen_timeout; }
bool Hal::isScreenSleeping() const { return false; }
uint32_t Hal::getLastWakeTime() const { return 0; }
void Hal::sleepScreen() {}
void Hal::wakeScreen() {}
void Hal::resetActivityTimer() {}
void Hal::display_tick() {}

LGFX_Device& Hal::getDisplay() { return _sim_display; }
LGFX_Sprite& Hal::getCanvas() { return _sim_canvas; }
void Hal::updateCanvas() {}

bool Hal::lvglLock() { return true; }
void Hal::lvglUnlock() {}
void Hal::startLvglUpdate() {}
void Hal::stopLvglUpdate() {}

Hal::TouchPoint Hal::getTouchPoint() { return {}; }

void Hal::setSpeakerVolume(int volume, bool) { _spk_volume = std::clamp(volume, 0, 100); }
int Hal::getSpeakerVolume(bool) { return _spk_volume; }
int Hal::getAudioSampleRate() { return 16000; }
void Hal::audioRecord(std::vector<int16_t>& data, uint16_t, float) { data.clear(); }
void Hal::audioPlay(std::vector<int16_t>&, bool) {}
bool Hal::isAudioPlaying() { return false; }
void Hal::setSpeakerEnable(bool) {}
bool Hal::audioRecordStream16k(std::vector<int16_t>&, uint16_t, float) { return false; }
void Hal::audioPushStreamPcm(const int16_t*, std::size_t, int) {}
void Hal::audioClearStream() {}
std::size_t Hal::audioGetStreamQueuedMs() { return 0; }
float Hal::audioGetCurrentRms() { return 0.0f; }

void Hal::updateAudioSpectrum()
{
    const float t = millis() * 0.004f;
    for (std::size_t i = 0; i < AudioSpectrumFrame::bandCount; ++i) {
        _sim_spectrum.bands[i] = 0.25f + 0.65f * std::abs(std::sin(t + static_cast<float>(i) * 0.45f));
    }
    _sim_spectrum.peakFrequencyHz = 440.0f + 220.0f * std::sin(t * 0.7f);
}

Hal::AudioSpectrumFrame Hal::getAudioSpectrum() const { return _sim_spectrum; }
void Hal::playBootSfx() {}
void Hal::vibrate(uint16_t, uint8_t) {}
void Hal::stopVibrate() {}

void Hal::updateImuData()
{
    const float t   = millis() * 0.002f;
    _imu_data.accelX = 0.15f * std::sin(t);
    _imu_data.accelY = 0.15f * std::cos(t * 0.8f);
    _imu_data.accelZ = 1.0f;
    _imu_data.gyroX  = 12.0f * std::sin(t * 1.3f);
    _imu_data.gyroY  = 10.0f * std::cos(t * 1.1f);
    _imu_data.gyroZ  = 5.0f * std::sin(t * 0.5f);
}

void Hal::syncRtcTimeToSystem() {}
void Hal::syncSystemTimeToRtc() {}
void Hal::sntp_init() {}
void Hal::sntp_sync() {}

DateYmd Hal::getDateYmd()
{
    std::time_t now = std::time(nullptr);
    std::tm* tm     = std::localtime(&now);
    if (!tm) return {};
    return {static_cast<uint16_t>(tm->tm_year + 1900), static_cast<uint8_t>(tm->tm_mon + 1),
            static_cast<uint8_t>(tm->tm_mday)};
}

bool Hal::setDateYmd(const DateYmd&) { return true; }

TimeHms Hal::getTimeHms()
{
    std::time_t now = std::time(nullptr);
    std::tm* tm     = std::localtime(&now);
    if (!tm) return {};
    return {static_cast<uint8_t>(tm->tm_hour), static_cast<uint8_t>(tm->tm_min), static_cast<uint8_t>(tm->tm_sec)};
}

bool Hal::setTimeHms(const TimeHms&) { return true; }
void Hal::setTimezone(std::string_view tz) { _sim_timezone = tz; }
std::string Hal::getTimezone() { return _sim_timezone; }
bool Hal::loadAlarmStorage(AlarmStorageSnapshot& snapshot) { snapshot = _sim_alarms; return true; }
bool Hal::saveAlarmStorage(const AlarmStorageSnapshot& snapshot) { _sim_alarms = snapshot; return true; }
void Hal::startAlarm() {}
void Hal::stopAlarm() {}

void Hal::updateButtonStates()
{
    const uint32_t now   = millis();
    const Uint8* keys    = SDL_GetKeyboardState(nullptr);
    const bool a_pressed = _sim_btn_raw[0] || (keys && (keys[SDL_SCANCODE_1] || keys[SDL_SCANCODE_A]));
    const bool b_pressed = _sim_btn_raw[1] || (keys && (keys[SDL_SCANCODE_2] || keys[SDL_SCANCODE_B]));
    const bool p_pressed = _sim_btn_raw[2] || (keys && (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_P]));

    btnA.setRawState(now, a_pressed);
    btnB.setRawState(now, b_pressed);
    btnPwr.setRawState(now, p_pressed);
}

void Hal::pwr_button_tick() {}
void Hal::button_on_sleep() {}
void Hal::setButtonConfig(ButtonConfig config, bool) { _btn_config = config; }
const Hal::ButtonConfig& Hal::getButtonConfig(bool) { return _btn_config; }

void Hal::setWifiEnabled(bool enabled, bool) { _sim_wifi_enabled = enabled; }
bool Hal::isWifiEnabled(bool) { return _sim_wifi_enabled; }
bool Hal::isWifiConnected() { return _sim_wifi_enabled; }

Hal::WifiStatus Hal::getWifiStatus()
{
    WifiStatus s;
    s.enabled   = _sim_wifi_enabled;
    s.mode      = _sim_wifi_enabled ? WifiMode::Connected : WifiMode::Off;
    s.connected = _sim_wifi_enabled;
    s.ssid      = "SimWiFi";
    s.ipAddress = "127.0.0.1";
    return s;
}

void Hal::wifi_enter_badge_ap() {}
void Hal::wifi_exit_badge_ap() {}
void Hal::wifi_on_screen_wake() {}

bool Hal::loadBadgeImage(lv_obj_t*) { return false; }
bool Hal::loadNextBadgeImage(lv_obj_t*) { return false; }
bool Hal::loadPreviousBadgeImage(lv_obj_t*) { return false; }
bool Hal::loadNextActiveBadgeImage(lv_obj_t*) { return false; }
uint32_t Hal::getBadgeCarouselIntervalMs() { return 5000; }
void Hal::setBadgeCarouselIntervalSeconds(uint32_t) {}
bool Hal::isBadgeSlotActive(std::size_t) { return false; }
bool Hal::setBadgeSlotActive(std::size_t, bool) { return false; }
bool Hal::startBadgeEditModeViaAp(std::function<void(std::string_view)>) { return false; }

bool Hal::shouldShowGuide() { return false; }

bool Hal::startOta(std::function<void(std::string_view)>) { return false; }
void Hal::stopOta() {}
bool Hal::isOtaRunning() { return false; }
Hal::OtaStatus Hal::getOtaStatus() { return {}; }

void Hal::setEyeCareReminder(bool enabled, bool) { _sim_eyecare = enabled; }
bool Hal::getEyeCareReminder(bool) { return _sim_eyecare; }
void Hal::eye_care_tick() {}
void Hal::playEyeCareDing() {}

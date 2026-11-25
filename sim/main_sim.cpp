#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/app_launcher/app_launcher.h>
#include <apps/app_stopwatch/app_stopwatch.h>
#include <apps/app_alarm_clock/app_alarm_clock.h>
#include <apps/app_imu/app_imu.h>
#include <apps/app_badge/app_badge.h>
#include <apps/app_setup/app_setup.h>
#include <hal/hal.h>
#include <lvgl.h>
#include <SDL.h>
#include <emscripten.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

static void sim_loop()
{
    lv_timer_handler();
    GetHAL().updateButtonStates();
    GetMooncake().update();
}

int main(int, char**)
{
    mclog::set_level(mclog::level_info);

    lv_init();
    lv_display_t* disp = lv_sdl_window_create(466, 466);
    lv_sdl_window_set_title(disp, "M5StopWatch Preview");
    lv_sdl_mouse_create();

    GetHAL().init();

    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    GetMooncake().installApp(std::make_unique<AppLauncher>());
    GetMooncake().installApp(std::make_unique<AppAlarmClock>());
    GetMooncake().installApp(std::make_unique<AppStopWatch>());
    GetMooncake().installApp(std::make_unique<AppBadge>());
    GetMooncake().installApp(std::make_unique<AppImu>());
    GetMooncake().installApp(std::make_unique<AppSetup>());

    emscripten_set_main_loop(sim_loop, 0, 1);
    return 0;
}

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_launcher.h"
#include <apps/common/status_bar/status_bar.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <cstdint>

using namespace mooncake;

void AppLauncher::onLauncherCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");

    open();
}

void AppLauncher::onLauncherOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    if (GetHAL().wokeFromDeepSleepStandby()) {
        _should_play_boot_sfx = false;
    }

    show_guide_page();

    {
        LvglLockGuard lock;
        create_launcher_view();
        view::create_status_bar(0xEDF4FF, 0x385179, true);
    }

    if (_should_play_boot_sfx) {
        _should_play_boot_sfx = false;
        GetHAL().playBootSfx();
    }

    _last_charge_check_tick = GetHAL().millis();
    _was_battery_charging   = GetHAL().isBatteryCharging();

    _is_first_open = false;
}

void AppLauncher::onLauncherRunning()
{
    if (GetHAL().isScreenSleeping()) {
        GetHAL().updateButtonStates();
        return;
    }

    LvglLockGuard lock;

    _view->update();
    view::update_status_bar();
}

void AppLauncher::onLauncherClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    _pending_status_bar_create = false;
    _status_bar_create_tick    = 0;
    _last_charge_check_tick    = 0;
    _was_battery_charging      = false;
    _view.reset();
    view::destroy_status_bar();
}

void AppLauncher::onLauncherDestroy()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}

void AppLauncher::create_launcher_view()
{
    _view = std::make_unique<view::LauncherView>();
    _view->init(getAppProps());
    _view->onAppClicked = [&](int appID) {
        mclog::tagInfo(getAppInfo().name, "handle open app, app id: {}", appID);
        openApp(appID);
    };
    _view->onStatusBarClicked = [&]() {
        view::show_status_bar();
    };
}

void AppLauncher::show_guide_page()
{
    if (!_is_first_open) {
        return;
    }

    if (GetHAL().wokeFromDeepSleepStandby()) {
        return;
    }

    if (!GetHAL().shouldShowGuide()) {
        return;
    }

    GetHAL().lvglLock();
    auto guide_page = std::make_unique<view::GuidePage>();
    GetHAL().lvglUnlock();

    _should_play_boot_sfx = false;
    GetHAL().playBootSfx();

    input::KeyManager key_manager;

    while (1) {
        GetHAL().feedTheDog();
        GetHAL().delay(50);

        key_manager.update();
        if (key_manager.getEvent() == input::KeyEvent::GoHome) {
            break;
        }
    }

    LvglLockGuard lock;
    guide_page.reset();
}

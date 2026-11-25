/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <mooncake_log.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <fmt/format.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

static std::string _tag = "LauncherView";

namespace {

// Soft pastel icon badge colors inspired by the reference UI
constexpr std::array<uint32_t, 9> _pastel_badge_colors = {
    0xE4F1E8,  // Soft mint
    0xF9E9DC,  // Soft peach/terracotta
    0xE5EEF8,  // Soft sky blue
    0xECE6F6,  // Soft lavender
    0xF7ECDF,  // Soft sand
    0xE3F0EA,  // Soft sage
    0xFAE6E3,  // Soft coral
    0xEDF2DF,  // Soft olive
    0xEAE6DF,  // Soft warm stone
};

}  // namespace

LauncherView::~LauncherView()
{
    _key_manager.reset();
    _icon_images.clear();
    _icon_badges.clear();
    _app_cards.clear();
    _orbit_time.reset();
    _orbit_hub.reset();
    _orbit_panel.reset();
    _bat_label.reset();
    _bat_lightning.reset();
    _bat_tip.reset();
    _bat_bar.reset();
    _bat_icon_wrap.reset();
    _bat_pill.reset();
    _wifi_label.reset();
    _wifi_icon.reset();
    _wifi_pill.reset();
    _drag_handle.reset();
    _status_bar.reset();
    _desktop_page.reset();
    _clock_faces.reset();
    _pager.reset();
    _panel.reset();
}

void LauncherView::init(std::vector<mooncake::AppProps_t> appProps)
{
    mclog::tagInfo(_tag, "init ({} apps)", appProps.size());

    _key_manager        = std::make_unique<input::KeyManager>();
    const auto& theme   = GetHAL().getTheme();
    const bool is_light = (GetHAL().getThemeMode() == Hal::ThemeMode::Light);
    _last_theme         = GetHAL().getThemeMode();

    /* ------------------------------ Screen setup ------------------------------ */
    ScreenActive screen;
    screen.removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    /* ---------------------------------- Panel --------------------------------- */
    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setAlign(LV_ALIGN_CENTER);
    _panel->setSize(466, 466);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgColor(lv_color_hex(theme.background));
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    /* ---------- Horizontal 2-Page Pager: [0: Clock Faces | 1: Desktop] -------- */
    _pager = std::make_unique<Container>(_panel->get());
    _pager->setSize(466, 466);
    _pager->setPos(0, 0);
    _pager->setRadius(0);
    _pager->setBorderWidth(0);
    _pager->setPaddingAll(0);
    _pager->setBgOpa(0);
    _pager->setScrollDir(LV_DIR_HOR);
    _pager->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_x(_pager->get(), LV_SCROLL_SNAP_CENTER);
    _pager->addFlag(LV_OBJ_FLAG_SCROLL_ONE);

    /* ---------------- Page 0 (Left, Default): Echo Clock Faces ---------------- */
    _clock_faces = std::make_unique<ClockFacesView>(_pager->get());

    /* ---------------- Page 1 (Right): App Grid Desktop ------------------------ */
    _desktop_page = std::make_unique<Container>(_pager->get());
    _desktop_page->setSize(466, 466);
    _desktop_page->setPos(466, 0);
    _desktop_page->setRadius(0);
    _desktop_page->setBorderWidth(0);
    _desktop_page->setPaddingAll(0);
    _desktop_page->setBgOpa(0);
    _desktop_page->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _desktop_page->addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    /* ---------------------------- Top drag handle ----------------------------- */
    _drag_handle = std::make_unique<Container>(_desktop_page->get());
    _drag_handle->setSize(34, 4);
    _drag_handle->align(LV_ALIGN_TOP_MID, 0, 18);
    _drag_handle->setRadius(2);
    _drag_handle->setBorderWidth(0);
    _drag_handle->setBgColor(lv_color_hex(theme.textMuted));
    _drag_handle->setBgOpa(LV_OPA_COVER);
    _drag_handle->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    /* --------------------- Symmetrical 3-Zone Status Header ------------------- */
    _status_bar = std::make_unique<Container>(_desktop_page->get());
    _status_bar->setSize(268, 34);
    _status_bar->align(LV_ALIGN_TOP_MID, 0, 38);
    _status_bar->setBgOpa(0);
    _status_bar->setBorderWidth(0);
    _status_bar->setPaddingAll(0);
    _status_bar->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _status_bar->addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    _status_bar->onClick().connect([this]() {
        if (_pager && lv_obj_is_scrolling(_pager->get())) {
            return;
        }
        if (onStatusBarClicked) {
            onStatusBarClicked();
        }
    });

    // Left symmetrical capsule: Wi-Fi icon + compact state ("On" / "..." / "AP" / "Off")
    _wifi_pill = std::make_unique<Container>(_status_bar->get());
    _wifi_pill->setSize(82, 28);
    _wifi_pill->align(LV_ALIGN_CENTER, -89, 0);
    _wifi_pill->setRadius(14);
    _wifi_pill->setBgColor(lv_color_hex(theme.cardBackground));
    _wifi_pill->setBgOpa(LV_OPA_COVER);
    _wifi_pill->setBorderWidth(1);
    _wifi_pill->setBorderColor(lv_color_hex(theme.border));
    _wifi_pill->setPaddingAll(0);
    _wifi_pill->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _wifi_pill->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(_wifi_pill->get(), LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_wifi_pill->get(), LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_wifi_pill->get(), 5, 0);

    _wifi_icon = std::make_unique<Label>(_wifi_pill->get());
    _wifi_icon->setText(LV_SYMBOL_WIFI);
    _wifi_icon->setTextFont(&lv_font_montserrat_16);
    _wifi_icon->setTextColor(lv_color_hex(theme.statusBarPrimary));
    _wifi_icon->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    _wifi_label = std::make_unique<Label>(_wifi_pill->get());
    _wifi_label->setText("Off");
    _wifi_label->setTextFont(&MontserratSemiBold16);
    _wifi_label->setTextColor(lv_color_hex(theme.textSecondary));
    _wifi_label->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    // Center time removed — orbit hub already shows HH:MM (avoids stacking above top app)

    // Right symmetrical capsule: Battery icon + percentage (mirrors _wifi_pill at +89)
    _bat_pill = std::make_unique<Container>(_status_bar->get());
    _bat_pill->setSize(82, 28);
    _bat_pill->align(LV_ALIGN_CENTER, 89, 0);
    _bat_pill->setRadius(14);
    _bat_pill->setBgColor(lv_color_hex(theme.cardBackground));
    _bat_pill->setBgOpa(LV_OPA_COVER);
    _bat_pill->setBorderWidth(1);
    _bat_pill->setBorderColor(lv_color_hex(theme.border));
    _bat_pill->setPaddingAll(0);
    _bat_pill->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _bat_pill->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(_bat_pill->get(), LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_bat_pill->get(), LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(_bat_pill->get(), 4, 0);

    _bat_icon_wrap = std::make_unique<Container>(_bat_pill->get());
    _bat_icon_wrap->setSize(26, 14);
    _bat_icon_wrap->setBgOpa(0);
    _bat_icon_wrap->setBorderWidth(0);
    _bat_icon_wrap->setPaddingAll(0);
    _bat_icon_wrap->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _bat_icon_wrap->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    _bat_bar = std::make_unique<Bar>(_bat_icon_wrap->get());
    _bat_bar->setSize(22, 12);
    _bat_bar->align(LV_ALIGN_LEFT_MID, 1, 0);
    _bat_bar->setRadius(3);
    _bat_bar->setRadius(2, LV_PART_INDICATOR);
    _bat_bar->setBgColor(lv_color_hex(theme.cardBackground));
    _bat_bar->setBgColor(lv_color_hex(0x2D6A4F), LV_PART_INDICATOR);
    _bat_bar->setBgOpa(LV_OPA_COVER);
    _bat_bar->setRange(0, 100);
    _bat_bar->setValue(80);
    _bat_bar->setPadding(1, 1, 1, 1);
    _bat_bar->setOutlineWidth(1);
    _bat_bar->setOutlineColor(lv_color_hex(theme.statusBarPrimary));
    _bat_bar->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    _bat_tip = std::make_unique<Container>(_bat_icon_wrap->get());
    _bat_tip->setSize(2, 5);
    _bat_tip->setRadius(1);
    _bat_tip->setBorderWidth(0);
    _bat_tip->setBgColor(lv_color_hex(theme.statusBarPrimary));
    _bat_tip->setBgOpa(LV_OPA_COVER);
    _bat_tip->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align_to(_bat_tip->get(), _bat_bar->get(), LV_ALIGN_OUT_RIGHT_MID, 1, 0);

    _bat_lightning = std::make_unique<Image>(_bat_icon_wrap->get());
    _bat_lightning->setSrc(&icon_bat_lightning);
    _bat_lightning->setImageRecolor(lv_color_hex(theme.statusBarPrimary));
    _bat_lightning->setImageRecolorOpa(LV_OPA_COVER);
    _bat_lightning->setHidden(true);
    _bat_lightning->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align_to(_bat_lightning->get(), _bat_bar->get(), LV_ALIGN_CENTER, 0, 0);

    _bat_label = std::make_unique<Label>(_bat_pill->get());
    _bat_label->setText("100%");
    _bat_label->setTextFont(&MontserratSemiBold16);
    _bat_label->setTextColor(lv_color_hex(theme.statusBarPrimary));
    _bat_label->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    update_status_info();
    _last_status_tick = GetHAL().millis();
    _last_anim_tick   = _last_status_tick;

    /* -------------------------- Orbital app ring ------------------------------ */
    _orbit_panel = std::make_unique<Container>(_desktop_page->get());
    _orbit_panel->setSize(466, 466);
    _orbit_panel->setPos(0, 0);
    _orbit_panel->setBgOpa(0);
    _orbit_panel->setBorderWidth(0);
    _orbit_panel->setPaddingAll(0);
    _orbit_panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _orbit_panel->addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    // Center hub at true dial center — status pills overlay, they must not shift the ring
    _orbit_hub = std::make_unique<Container>(_orbit_panel->get());
    _orbit_hub->setSize(108, 108);
    _orbit_hub->align(LV_ALIGN_CENTER, 0, 0);
    _orbit_hub->setRadius(54);
    _orbit_hub->setBgColor(lv_color_hex(theme.cardBackground));
    _orbit_hub->setBgOpa(is_light ? LV_OPA_70 : LV_OPA_COVER);
    _orbit_hub->setBorderWidth(1);
    _orbit_hub->setBorderColor(lv_color_hex(theme.border));
    _orbit_hub->setPaddingAll(0);
    _orbit_hub->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _orbit_hub->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _orbit_hub->addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    _orbit_time = std::make_unique<Label>(_orbit_hub->get());
    _orbit_time->setText("00:00");
    _orbit_time->setTextFont(&MontserratSemiBold26);
    _orbit_time->setTextColor(lv_color_hex(theme.textPrimary));
    _orbit_time->align(LV_ALIGN_CENTER, 0, 0);
    _orbit_time->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    // Icon-only ring: badge centers near the bezel (no app name labels)
    // 466/2=233, badge r≈43 → radius ~165 fills the face with a small bezel margin
    constexpr int kOrbitRadius    = 165;
    constexpr int kBadgeSize      = 86;
    constexpr float kPi           = 3.14159265f;
    constexpr uint32_t kDarkPress = 0x15181C;

    const size_t total = appProps.size();
    for (size_t i = 0; i < total; i++) {
        const auto& props     = appProps[i];
        const float angle_deg = -90.0f + (360.0f * static_cast<float>(i) / static_cast<float>(total));
        const float rad       = angle_deg * kPi / 180.0f;
        const int ox          = static_cast<int>(std::lround(std::cos(rad) * kOrbitRadius));
        const int oy          = static_cast<int>(std::lround(std::sin(rad) * kOrbitRadius));

        auto card = std::make_unique<Container>(_orbit_panel->get());
        card->setSize(kBadgeSize, kBadgeSize);
        card->align(LV_ALIGN_CENTER, ox, oy);
        card->setBgOpa(0);
        card->setBorderWidth(0);
        card->setPaddingAll(0);
        card->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        card->addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

        auto app_id = props.appID;
        card->onClick().connect([this, app_id]() {
            if (_pager && lv_obj_is_scrolling(_pager->get())) {
                return;
            }
            if (auto* indev = lv_indev_active()) {
                if (lv_indev_get_scroll_obj(indev) != nullptr) {
                    return;
                }
            }
            _clicked_app_id = app_id;
        });

        const uint32_t badge_bg = _pastel_badge_colors[i % _pastel_badge_colors.size()];
        auto badge              = std::make_unique<Container>(card->get());
        badge->setSize(kBadgeSize, kBadgeSize);
        badge->align(LV_ALIGN_CENTER, 0, 0);
        badge->setRadius(kBadgeSize / 2);
        badge->setBgColor(lv_color_hex(badge_bg));
        badge->setBgOpa(LV_OPA_COVER);
        badge->setBorderWidth(0);
        badge->setPaddingAll(0);
        badge->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        badge->removeFlag(LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_clip_corner(badge->get(), true, 0);
        lv_obj_set_style_bg_color(badge->get(), lv_color_hex(is_light ? theme.cardPressed : kDarkPress),
                                  LV_STATE_PRESSED);

        if (props.info.icon != nullptr) {
            auto img = std::make_unique<Image>(badge->get());
            img->setSrc(props.info.icon);
            img->align(LV_ALIGN_CENTER, 0, 0);  // Native 72x72 1:1 raster
            img->removeFlag(LV_OBJ_FLAG_CLICKABLE);
            _icon_images.push_back(std::move(img));
        }

        _icon_badges.push_back(std::move(badge));
        _app_cards.push_back(std::move(card));
    }

    // Keep status header above the orbit ring for hit-testing & visibility
    if (_drag_handle) {
        lv_obj_move_foreground(_drag_handle->get());
    }
    if (_status_bar) {
        lv_obj_move_foreground(_status_bar->get());
    }

    // Ensure launcher starts on Page 0 (Left Clock Page) on boot & Home return
    lv_obj_update_layout(_pager->get());
    lv_obj_scroll_to_x(_pager->get(), 0, LV_ANIM_OFF);

    // Destroy boot logo label
    GetHAL().bootLogo.reset();
}

bool LauncherView::isOnClockPage() const
{
    if (!_pager) {
        return true;
    }
    return lv_obj_get_scroll_x(_pager->get()) < 233;
}

void LauncherView::scrollToClock(bool anim)
{
    if (_pager) {
        lv_obj_scroll_to_x(_pager->get(), 0, anim ? LV_ANIM_ON : LV_ANIM_OFF);
    }
}

void LauncherView::update()
{
    if (_key_manager) {
        switch (_key_manager->update()) {
        case input::KeyEvent::GoHome:
            if (!isOnClockPage()) {
                scrollToClock(true);
            }
            break;
        case input::KeyEvent::GoPrevious:
            if (isOnClockPage() && _clock_faces) {
                _clock_faces->prevFace();
            }
            break;
        case input::KeyEvent::GoNext:
            if (isOnClockPage() && _clock_faces) {
                _clock_faces->nextFace();
            }
            break;
        default:
            break;
        }
    }

    if (_clicked_app_id != -1) {
        if (onAppClicked) {
            onAppClicked(_clicked_app_id);
        }
        _clicked_app_id = -1;
    }

    if (GetHAL().getThemeMode() != _last_theme) {
        apply_theme();
    }

    if (_clock_faces) {
        _clock_faces->update();
    }

    uint32_t now = GetHAL().millis();
    if (now - _last_anim_tick >= 380) {
        _last_anim_tick = now;
        if (GetHAL().getWifiStatus().mode == Hal::WifiMode::Connecting) {
            _conn_anim_step = (_conn_anim_step + 1) % 3;
            update_status_info();
            _last_status_tick = now;
        }
    }
    if (now - _last_status_tick >= 1000) {
        _last_status_tick = now;
        update_status_info();
    }
}

void LauncherView::apply_theme()
{
    const auto mode     = GetHAL().getThemeMode();
    const auto& theme   = GetHAL().getTheme();
    const bool is_light = (mode == Hal::ThemeMode::Light);
    _last_theme         = mode;

    if (_panel) {
        _panel->setBgColor(lv_color_hex(theme.background));
    }
    if (_clock_faces) {
        _clock_faces->applyTheme();
    }
    if (_drag_handle) {
        _drag_handle->setBgColor(lv_color_hex(theme.textMuted));
    }
    if (_wifi_pill) {
        _wifi_pill->setBgColor(lv_color_hex(theme.cardBackground));
        _wifi_pill->setBorderColor(lv_color_hex(theme.border));
    }
    if (_bat_pill) {
        _bat_pill->setBgColor(lv_color_hex(theme.cardBackground));
        _bat_pill->setBorderColor(lv_color_hex(theme.border));
    }
    if (_bat_label) {
        _bat_label->setTextColor(lv_color_hex(theme.statusBarPrimary));
    }
    if (_bat_bar) {
        _bat_bar->setBgColor(lv_color_hex(theme.cardBackground));
        _bat_bar->setOutlineColor(lv_color_hex(theme.statusBarPrimary));
    }
    if (_bat_tip) {
        _bat_tip->setBgColor(lv_color_hex(theme.statusBarPrimary));
    }
    if (_bat_lightning) {
        _bat_lightning->setImageRecolor(lv_color_hex(theme.statusBarPrimary));
    }
    if (_orbit_hub) {
        _orbit_hub->setBgColor(lv_color_hex(theme.cardBackground));
        _orbit_hub->setBgOpa(is_light ? LV_OPA_70 : LV_OPA_COVER);
        _orbit_hub->setBorderColor(lv_color_hex(theme.border));
    }
    if (_orbit_time) {
        _orbit_time->setTextColor(lv_color_hex(theme.textPrimary));
    }
    constexpr uint32_t kDarkPress = 0x15181C;
    for (size_t i = 0; i < _icon_badges.size(); ++i) {
        if (_icon_badges[i]) {
            // Keep pastel discs in dark mode — charcoal glyphs need a light plate
            const uint32_t badge_bg = _pastel_badge_colors[i % _pastel_badge_colors.size()];
            _icon_badges[i]->setBgColor(lv_color_hex(badge_bg));
            lv_obj_set_style_bg_color(_icon_badges[i]->get(), lv_color_hex(is_light ? theme.cardPressed : kDarkPress),
                                      LV_STATE_PRESSED);
        }
    }
    update_status_info();
}

void LauncherView::update_status_info()
{
    const auto& theme      = GetHAL().getTheme();
    const bool is_light    = (GetHAL().getThemeMode() == Hal::ThemeMode::Light);
    const auto wifi_status = GetHAL().getWifiStatus();

    if (_wifi_icon && _wifi_label) {
        switch (wifi_status.mode) {
        case Hal::WifiMode::Connected: {
            uint32_t ok_col = is_light ? 0x2D6A4F : 0x4AD78C;
            _wifi_icon->setTextColor(lv_color_hex(ok_col));
            _wifi_icon->setOpa(LV_OPA_COVER);
            _wifi_label->setWidth(LV_SIZE_CONTENT);
            _wifi_label->setText("On");
            _wifi_label->setTextColor(lv_color_hex(theme.statusBarPrimary));
            break;
        }
        case Hal::WifiMode::Connecting: {
            static constexpr const char* kDots[3] = {".", "..", "..."};
            uint32_t warn_col                     = is_light ? 0xB85D19 : 0xFFB74D;
            _wifi_icon->setTextColor(lv_color_hex(warn_col));
            _wifi_icon->setOpa((_conn_anim_step == 1) ? LV_OPA_60 : LV_OPA_COVER);
            // Fixed width keeps the icon anchored steadily while dots animate
            _wifi_label->setWidth(20);
            _wifi_label->setText(kDots[_conn_anim_step % 3]);
            _wifi_label->setTextColor(lv_color_hex(warn_col));
            break;
        }
        case Hal::WifiMode::ConfigAp:
        case Hal::WifiMode::BadgeAp: {
            _wifi_icon->setTextColor(lv_color_hex(theme.accent));
            _wifi_icon->setOpa(LV_OPA_COVER);
            _wifi_label->setWidth(LV_SIZE_CONTENT);
            _wifi_label->setText("AP");
            _wifi_label->setTextColor(lv_color_hex(theme.accent));
            break;
        }
        case Hal::WifiMode::Off:
        case Hal::WifiMode::Error:
        default: {
            _wifi_icon->setTextColor(lv_color_hex(theme.textMuted));
            _wifi_icon->setOpa(LV_OPA_70);
            _wifi_label->setWidth(LV_SIZE_CONTENT);
            _wifi_label->setText("Off");
            _wifi_label->setTextColor(lv_color_hex(theme.textSecondary));
            break;
        }
        }
    }

    const auto t = GetHAL().getTimeHms();
    if (_orbit_time) {
        _orbit_time->setText(fmt::format("{:02d}:{:02d}", t.hour, t.minute));
    }

    const uint8_t bat_level = GetHAL().getBatteryLevel();
    const bool charging     = GetHAL().isBatteryCharging();
    if (_bat_bar) {
        _bat_bar->setValue(bat_level);
        _bat_bar->setBgColor(lv_color_hex(charging ? 0x19C25F : 0x2D6A4F), LV_PART_INDICATOR);
    }
    if (_bat_lightning) {
        _bat_lightning->setHidden(!charging);
    }
    if (_bat_label) {
        _bat_label->setText(fmt::format("{}%", bat_level));
    }
}

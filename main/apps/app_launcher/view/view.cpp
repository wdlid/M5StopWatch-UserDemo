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

const char* wifi_status_text(Hal::WifiMode mode)
{
    switch (mode) {
    case Hal::WifiMode::Connected:
        return "Connected";
    case Hal::WifiMode::Connecting:
        return "Connecting";
    case Hal::WifiMode::ConfigAp:
    case Hal::WifiMode::BadgeAp:
        return "AP Mode";
    case Hal::WifiMode::Off:
    case Hal::WifiMode::Error:
    default:
        return "Offline";
    }
}

}  // namespace

LauncherView::~LauncherView()
{
    _key_manager.reset();
    _app_labels.clear();
    _icon_images.clear();
    _icon_badges.clear();
    _app_cards.clear();
    _grid_panel.reset();
    _bat_label.reset();
    _bat_lightning.reset();
    _bat_tip.reset();
    _bat_bar.reset();
    _time_label.reset();
    _wifi_label.reset();
    _wifi_icon.reset();
    _drag_handle.reset();
    _status_bar.reset();
    _panel.reset();
}

void LauncherView::init(std::vector<mooncake::AppProps_t> appProps)
{
    mclog::tagInfo(_tag, "init ({} apps)", appProps.size());

    _key_manager = std::make_unique<input::KeyManager>();
    const auto& theme = GetHAL().getTheme();
    const bool is_light = (GetHAL().getThemeMode() == Hal::ThemeMode::Light);

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

    /* ---------------------------- Top drag handle ----------------------------- */
    _drag_handle = std::make_unique<Container>(_panel->get());
    _drag_handle->setSize(34, 4);
    _drag_handle->align(LV_ALIGN_TOP_MID, 0, 12);
    _drag_handle->setRadius(2);
    _drag_handle->setBorderWidth(0);
    _drag_handle->setBgColor(lv_color_hex(theme.textMuted));
    _drag_handle->setBgOpa(LV_OPA_COVER);
    _drag_handle->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    /* --------------------------- Direct Status Bar ---------------------------- */
    _status_bar = std::make_unique<Container>(_panel->get());
    _status_bar->setSize(290, 36);
    _status_bar->align(LV_ALIGN_TOP_MID, 0, 24);
    _status_bar->setBgOpa(0);
    _status_bar->setBorderWidth(0);
    _status_bar->setPaddingAll(0);
    _status_bar->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _status_bar->onClick().connect([this]() {
        if (onStatusBarClicked) {
            onStatusBarClicked();
        }
    });

    // Left: Wi-Fi icon + status
    _wifi_icon = std::make_unique<Label>(_status_bar->get());
    _wifi_icon->setText(LV_SYMBOL_WIFI);
    _wifi_icon->setTextFont(&lv_font_montserrat_14);
    _wifi_icon->setTextColor(lv_color_hex(theme.statusBarPrimary));
    _wifi_icon->align(LV_ALIGN_LEFT_MID, 4, 0);

    _wifi_label = std::make_unique<Label>(_status_bar->get());
    _wifi_label->setText("Offline");
    _wifi_label->setTextFont(&lv_font_montserrat_14);
    _wifi_label->setTextColor(lv_color_hex(theme.textSecondary));
    _wifi_label->align(LV_ALIGN_LEFT_MID, 24, 0);

    // Center: Time HH:MM
    _time_label = std::make_unique<Label>(_status_bar->get());
    _time_label->setText("00:00");
    _time_label->setTextFont(&lv_font_montserrat_18);
    _time_label->setTextColor(lv_color_hex(theme.textPrimary));
    _time_label->align(LV_ALIGN_CENTER, 0, 0);

    // Right: Battery icon + percentage
    _bat_label = std::make_unique<Label>(_status_bar->get());
    _bat_label->setText("100%");
    _bat_label->setTextFont(&lv_font_montserrat_14);
    _bat_label->setTextColor(lv_color_hex(theme.statusBarPrimary));
    _bat_label->align(LV_ALIGN_RIGHT_MID, -4, 0);

    _bat_bar = std::make_unique<Bar>(_status_bar->get());
    _bat_bar->setSize(24, 11);
    _bat_bar->align(LV_ALIGN_RIGHT_MID, -46, 0);
    _bat_bar->setRadius(3);
    _bat_bar->setRadius(2, LV_PART_INDICATOR);
    _bat_bar->setBgColor(lv_color_hex(theme.background));
    _bat_bar->setBgColor(lv_color_hex(0x2D6A4F), LV_PART_INDICATOR);
    _bat_bar->setBgOpa(LV_OPA_COVER);
    _bat_bar->setRange(0, 100);
    _bat_bar->setValue(80);
    _bat_bar->setPadding(1, 1, 1, 1);
    _bat_bar->setOutlineWidth(1);
    _bat_bar->setOutlineColor(lv_color_hex(theme.statusBarPrimary));

    _bat_tip = std::make_unique<Container>(_status_bar->get());
    _bat_tip->setSize(3, 5);
    _bat_tip->setRadius(1);
    _bat_tip->setBorderWidth(0);
    _bat_tip->setBgColor(lv_color_hex(theme.statusBarPrimary));
    _bat_tip->setBgOpa(LV_OPA_COVER);
    lv_obj_align_to(_bat_tip->get(), _bat_bar->get(), LV_ALIGN_OUT_RIGHT_MID, 1, 0);

    _bat_lightning = std::make_unique<Image>(_status_bar->get());
    _bat_lightning->setSrc(&icon_bat_lightning);
    _bat_lightning->setImageRecolor(lv_color_hex(theme.statusBarPrimary));
    _bat_lightning->setImageRecolorOpa(LV_OPA_COVER);
    _bat_lightning->setHidden(true);
    lv_obj_align_to(_bat_lightning->get(), _bat_bar->get(), LV_ALIGN_CENTER, 0, 0);

    update_status_info();
    _last_status_tick = GetHAL().millis();

    /* ----------------------------- 2-Column Grid ------------------------------ */
    _grid_panel = std::make_unique<Container>(_panel->get());
    _grid_panel->setSize(466, 376);
    _grid_panel->align(LV_ALIGN_TOP_MID, 0, 68);
    _grid_panel->setBgOpa(0);
    _grid_panel->setBorderWidth(0);
    _grid_panel->setPaddingAll(0);
    _grid_panel->setScrollDir(LV_DIR_VER);
    _grid_panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);

    constexpr int card_w      = 158;
    constexpr int card_h      = 54;
    constexpr int col_gap     = 12;
    constexpr int row_gap     = 10;
    constexpr int top_margin  = 6;
    constexpr int last_card_w = 176;

    const size_t total = appProps.size();
    const bool has_odd_last = (total % 2 == 1);

    for (size_t i = 0; i < total; i++) {
        const auto& props = appProps[i];
        const size_t row  = i / 2;
        const size_t col  = i % 2;
        const bool is_centered_last = (has_odd_last && i == total - 1);

        int w = is_centered_last ? last_card_w : card_w;
        int x = is_centered_last ? 0 : (col == 0 ? -(card_w / 2 + col_gap / 2) : (card_w / 2 + col_gap / 2));
        int y = top_margin + static_cast<int>(row) * (card_h + row_gap);

        auto card = std::make_unique<Container>(_grid_panel->get());
        card->setSize(w, card_h);
        card->align(LV_ALIGN_TOP_MID, x, y);
        card->setRadius(16);
        card->setBgColor(lv_color_hex(theme.cardBackground));
        card->setBgOpa(LV_OPA_COVER);
        card->setBorderWidth(1);
        card->setBorderColor(lv_color_hex(theme.border));
        card->setPaddingAll(0);
        card->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        card->addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_set_style_bg_color(card->get(), lv_color_hex(theme.cardPressed), LV_STATE_PRESSED);

        auto app_id = props.appID;
        card->onClick().connect([this, app_id]() {
            if (lv_obj_is_scrolling(_grid_panel->get())) {
                return;
            }
            if (auto* indev = lv_indev_active()) {
                if (lv_indev_get_scroll_obj(indev) != nullptr) {
                    return;
                }
            }
            _clicked_app_id = app_id;
        });

        // Icon badge on left
        uint32_t badge_bg = is_light ? _pastel_badge_colors[i % _pastel_badge_colors.size()] : theme.cardPressed;
        auto badge        = std::make_unique<Container>(card->get());
        badge->setSize(34, 34);
        badge->align(LV_ALIGN_LEFT_MID, 10, 0);
        badge->setRadius(10);
        badge->setBgColor(lv_color_hex(badge_bg));
        badge->setBgOpa(LV_OPA_COVER);
        badge->setBorderWidth(0);
        badge->setPaddingAll(0);
        badge->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        badge->removeFlag(LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_clip_corner(badge->get(), true, 0);

        if (props.info.icon != nullptr) {
            auto img = std::make_unique<Image>(badge->get());
            img->setSrc(props.info.icon);
            img->align(LV_ALIGN_CENTER, 0, 0);
            lv_image_set_scale(img->get(), 40);  // 200px * 40 / 256 ≈ 31px
            lv_image_set_antialias(img->get(), true);
            img->removeFlag(LV_OBJ_FLAG_CLICKABLE);
            _icon_images.push_back(std::move(img));
        }

        // App name on right
        auto label = std::make_unique<Label>(card->get());
        label->setText(props.info.name);
        label->setTextFont(&lv_font_montserrat_14);
        label->setTextColor(lv_color_hex(theme.textPrimary));
        label->setWidth(w - 56);
        lv_label_set_long_mode(label->get(), LV_LABEL_LONG_DOT);
        label->align(LV_ALIGN_LEFT_MID, 50, 0);
        label->removeFlag(LV_OBJ_FLAG_CLICKABLE);

        _icon_badges.push_back(std::move(badge));
        _app_labels.push_back(std::move(label));
        _app_cards.push_back(std::move(card));
    }

    // Destroy boot logo label
    GetHAL().bootLogo.reset();
}

void LauncherView::update()
{
    if (_key_manager) {
        switch (_key_manager->update()) {
        case input::KeyEvent::GoPrevious:
            if (_grid_panel) {
                _grid_panel->scrollBy(0, 64, LV_ANIM_ON);
            }
            break;
        case input::KeyEvent::GoNext:
            if (_grid_panel) {
                _grid_panel->scrollBy(0, -64, LV_ANIM_ON);
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

    uint32_t now = GetHAL().millis();
    if (now - _last_status_tick >= 1000) {
        _last_status_tick = now;
        update_status_info();
    }
}

void LauncherView::update_status_info()
{
    const auto wifi_status = GetHAL().getWifiStatus();
    if (_wifi_icon) {
        bool dim = (wifi_status.mode == Hal::WifiMode::Off || wifi_status.mode == Hal::WifiMode::Error);
        _wifi_icon->setOpa(dim ? LV_OPA_40 : LV_OPA_COVER);
    }
    if (_wifi_label) {
        _wifi_label->setText(wifi_status_text(wifi_status.mode));
    }

    const auto t = GetHAL().getTimeHms();
    if (_time_label) {
        _time_label->setText(fmt::format("{:02d}:{:02d}", t.hour, t.minute));
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

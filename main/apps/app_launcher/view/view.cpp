/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <mooncake_log.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <esp_heap_caps.h>
#include <fmt/format.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace view;
using namespace uitk;
using namespace uitk::lvgl_cpp;

static std::string _tag = "LauncherView";

namespace {

struct MacaronSwatch {
    uint32_t lightBg;
    uint32_t lightBorder;
    uint32_t lightPressed;
    uint32_t darkBg;
    uint32_t darkBorder;
    uint32_t darkPressed;
};

// 9-color Macaron palette:
// - Light mode uses the pastel macaron colors directly with a delicate crust border.
// - Dark mode uses hue-preserved "Dusk Velvet Macaron" tones (~50% lower luminance to prevent
//   AMOLED glare on #000000 while keeping >=3.3:1..6.3:1 contrast for the #3A3530 charcoal glyphs).
constexpr std::array<MacaronSwatch, 9> _macaron_swatches = {{
    // 0: 草莓牛奶粉 Strawberry Milk Pink (Warm) — Gemini
    {0xF8C8DC, 0xE6AFC6, 0xEBA8C4, 0xC88CA6, 0xDBA4BC, 0xA66E87},
    // 1: 蜜桃橘 Peach Orange (Warm) — AlarmClock
    {0xFFD3B6, 0xECBA98, 0xF2B892, 0xCFA082, 0xE2B79B, 0xAD7F62},
    // 2: 天空蓝 Sky Blue (Cool) — StopWatch
    {0xBFE0F5, 0xA2C9E4, 0x9EC8E6, 0x82AFCB, 0x9BC4DE, 0x648FA9},
    // 3: 薰衣草紫 Lavender Purple (Cool) — Badge
    {0xD5C6EA, 0xBDB0D8, 0xBEACDC, 0xA190BE, 0xB8A8D3, 0x82719E},
    // 4: 薄荷绿 Mint Green (Cool) — IMU
    {0xBDE8D5, 0x9FD1BB, 0x9CD4BC, 0x7EBAA2, 0x97CEB8, 0x609982},
    // 5: 奶油柠檬黄 Creamy Lemon Yellow (Warm) — Setup
    {0xFFF1A8, 0xE3CE74, 0xF0DC7D, 0xD1BE74, 0xE3D28E, 0xAF9C55},
    // 6: 玫瑰粉 Rose Pink (Warm)
    {0xE8A0B8, 0xD286A0, 0xD684A0, 0xB8748D, 0xCC8BA3, 0x96586F},
    // 7: 开心果绿 Pistachio Green (Cool)
    {0xD4E7B0, 0xBAD092, 0xBDD592, 0x9FB878, 0xB5CC90, 0x80985B},
    // 8: 莓果紫 Berry Purple (Cool)
    {0xB89AD0, 0xA081BA, 0xA280BE, 0x9175AD, 0xA88DC3, 0x73588E},
}};

size_t resolve_macaron_swatch(const void* icon_src, size_t fallback_index)
{
    if (icon_src == &icon_gemini) {
        return 0;
    }
    if (icon_src == &icon_clock) {
        return 1;
    }
    if (icon_src == &icon_stopwatch) {
        return 2;
    }
    if (icon_src == &icon_badge) {
        return 3;
    }
    if (icon_src == &icon_imu) {
        return 4;
    }
    if (icon_src == &icon_setup) {
        return 5;
    }
    return (6 + fallback_index) % _macaron_swatches.size();
}

void style_macaron_badge(lv_obj_t* badge_obj, const MacaronSwatch& swatch, bool is_light)
{
    const uint32_t bg      = is_light ? swatch.lightBg : swatch.darkBg;
    const uint32_t border  = is_light ? swatch.lightBorder : swatch.darkBorder;
    const uint32_t pressed = is_light ? swatch.lightPressed : swatch.darkPressed;
    lv_obj_set_style_bg_color(badge_obj, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_border_color(badge_obj, lv_color_hex(border), LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge_obj, lv_color_hex(pressed), LV_STATE_PRESSED);
}

constexpr int32_t kDesktopSize          = 466;
constexpr int32_t kGradMaxCoord         = kDesktopSize - 1;       // 465
constexpr size_t kGradLutLen            = 2 * kGradMaxCoord + 1;  // 931
constexpr size_t kGradRowPhases         = 4;
constexpr uint32_t kLightDesktopSurface = 0xF7FAFC;
constexpr uint32_t kLightDesktopBorder  = 0xD8E4F0;
constexpr uint32_t kLightDragHandle     = 0xF0F7FF;

bool intersect_area(lv_area_t& out, const lv_area_t& a, const lv_area_t& b)
{
    out.x1 = std::max(a.x1, b.x1);
    out.y1 = std::max(a.y1, b.y1);
    out.x2 = std::min(a.x2, b.x2);
    out.y2 = std::min(a.y2, b.y2);
    return out.x1 <= out.x2 && out.y1 <= out.y2;
}

// Precomputes CSS linear-gradient(45deg, #00c9ff 0%, #92fe9d 25%, #fc00ff 50%, #00c9ff 100%)
// on a 466x466 square into 4 Bayer-dithered 1D diagonal LUTs (4 * 931 * 2 = 7,448 bytes).
// For any local pixel (lx, ly), projection k = (465 - ly) + lx, so every scanline is a single
// contiguous memcpy from &lut[(ly & 3) * 931 + (465 - ly) + lx1].
void build_light_grad_lut(uint16_t* lut)
{
    struct GradStop {
        float pos;
        float r;
        float g;
        float b;
    };
    static constexpr std::array<GradStop, 4> kStops = {{
        {0.00f, 0x00, 0xC9, 0xFF},  // #00c9ff at 0%
        {0.25f, 0x92, 0xFE, 0x9D},  // #92fe9d at 25%
        {0.50f, 0xFC, 0x00, 0xFF},  // #fc00ff at 50%
        {1.00f, 0x00, 0xC9, 0xFF},  // #00c9ff at 100%
    }};
    static constexpr uint8_t kBayer4x4[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5},
    };

    for (size_t k = 0; k < kGradLutLen; ++k) {
        const float t = static_cast<float>(k) / static_cast<float>(kGradLutLen - 1);
        size_t seg    = 0;
        if (t >= kStops[2].pos) {
            seg = 2;
        } else if (t >= kStops[1].pos) {
            seg = 1;
        }
        const auto& s0    = kStops[seg];
        const auto& s1    = kStops[seg + 1];
        const float local = (t - s0.pos) / (s1.pos - s0.pos);
        const float cr    = s0.r + (s1.r - s0.r) * local;
        const float cg    = s0.g + (s1.g - s0.g) * local;
        const float cb    = s0.b + (s1.b - s0.b) * local;

        for (size_t r = 0; r < kGradRowPhases; ++r) {
            // Since k = 465 - ly + lx and 464 is a multiple of 4, (k + r - 1) & 3 == lx & 3
            const size_t x_mod  = (k + r + 3) & 3;
            const float dither  = (static_cast<float>(kBayer4x4[r][x_mod]) + 0.5f) * (1.0f / 16.0f);
            const uint16_t r5   = static_cast<uint16_t>(std::clamp(static_cast<int>(cr * (31.0f / 255.0f) + dither), 0, 31));
            const uint16_t g6   = static_cast<uint16_t>(std::clamp(static_cast<int>(cg * (63.0f / 255.0f) + dither), 0, 63));
            const uint16_t b5   = static_cast<uint16_t>(std::clamp(static_cast<int>(cb * (31.0f / 255.0f) + dither), 0, 31));
            lut[r * kGradLutLen + k] = static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
        }
    }
}

}  // namespace

LauncherView::~LauncherView()
{
    _key_manager.reset();
    _badge_swatch_indices.clear();
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
    if (_light_grad_lut) {
        heap_caps_free(_light_grad_lut);
        _light_grad_lut = nullptr;
    }
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
    lv_obj_remove_flag(_pager->get(), LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_snap_x(_pager->get(), LV_SCROLL_SNAP_CENTER);
    _pager->addFlag(LV_OBJ_FLAG_SCROLL_ONE);

    lv_obj_add_event_cb(
        _pager->get(),
        [](lv_event_t* e) {
            auto* self = static_cast<LauncherView*>(lv_event_get_user_data(e));
            if (!self) {
                return;
            }
            const auto code = lv_event_get_code(e);
            if (code == LV_EVENT_SCROLL_BEGIN) {
                self->_pager_scrolling = true;
                if (self->_clock_faces) {
                    self->_clock_faces->setPagerScrolling(true);
                }
            } else if (code == LV_EVENT_SCROLL_END) {
                self->_pager_scrolling = false;
                if (self->_clock_faces) {
                    self->_clock_faces->setPagerScrolling(false);
                }
            }
        },
        LV_EVENT_ALL, this);

    /* ---------------- Page 0 (Left, Default): Echo Clock Faces ---------------- */
    _clock_faces = std::make_unique<ClockFacesView>(_pager->get());

    /* ---------------- Page 1 (Right): App Grid Desktop ------------------------ */
    const uint32_t surface_col = is_light ? kLightDesktopSurface : theme.cardBackground;
    const uint32_t border_col  = is_light ? kLightDesktopBorder : theme.border;
    if (is_light) {
        ensure_light_grad_lut();
    }

    _desktop_page = std::make_unique<Container>(_pager->get());
    _desktop_page->setSize(466, 466);
    _desktop_page->setPos(466, 0);
    _desktop_page->setRadius(0);
    _desktop_page->setBorderWidth(0);
    _desktop_page->setPaddingAll(0);
    _desktop_page->setBgColor(lv_color_hex(theme.background));
    _desktop_page->setBgOpa(is_light ? LV_OPA_TRANSP : LV_OPA_COVER);
    _desktop_page->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _desktop_page->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _desktop_page->addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_add_event_cb(_desktop_page->get(), desktop_cover_check_cb,
                        static_cast<lv_event_code_t>(LV_EVENT_COVER_CHECK | LV_EVENT_PREPROCESS), this);
    lv_obj_add_event_cb(_desktop_page->get(), desktop_draw_main_cb, LV_EVENT_DRAW_MAIN, this);

    /* ---------------------------- Top drag handle ----------------------------- */
    _drag_handle = std::make_unique<Container>(_desktop_page->get());
    _drag_handle->setSize(34, 4);
    _drag_handle->align(LV_ALIGN_TOP_MID, 0, 18);
    _drag_handle->setRadius(2);
    _drag_handle->setBorderWidth(0);
    _drag_handle->setBgColor(lv_color_hex(is_light ? kLightDragHandle : theme.textMuted));
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
    _wifi_pill->setBgColor(lv_color_hex(surface_col));
    _wifi_pill->setBgOpa(LV_OPA_COVER);
    _wifi_pill->setBorderWidth(1);
    _wifi_pill->setBorderColor(lv_color_hex(border_col));
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
    _bat_pill->setBgColor(lv_color_hex(surface_col));
    _bat_pill->setBgOpa(LV_OPA_COVER);
    _bat_pill->setBorderWidth(1);
    _bat_pill->setBorderColor(lv_color_hex(border_col));
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
    _bat_bar->setBgColor(lv_color_hex(surface_col));
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

    update_status_info(true);
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
    _orbit_panel->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _orbit_panel->addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    // Center hub at true dial center — opaque pre-blended fill avoids 108x108 alpha blend per scroll frame
    _orbit_hub = std::make_unique<Container>(_orbit_panel->get());
    _orbit_hub->setSize(108, 108);
    _orbit_hub->align(LV_ALIGN_CENTER, 0, 0);
    _orbit_hub->setRadius(54);
    _orbit_hub->setBgColor(lv_color_hex(surface_col));
    _orbit_hub->setBgOpa(LV_OPA_COVER);
    _orbit_hub->setBorderWidth(1);
    _orbit_hub->setBorderColor(lv_color_hex(border_col));
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
    constexpr int kOrbitRadius = 165;
    constexpr int kBadgeSize   = 86;
    constexpr float kPi        = 3.14159265f;

    _badge_swatch_indices.clear();
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
        card->addFlag(LV_OBJ_FLAG_STATE_TRICKLE);

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

        const size_t swatch_idx = resolve_macaron_swatch(props.info.icon, i);
        _badge_swatch_indices.push_back(swatch_idx);

        auto badge = std::make_unique<Container>(card->get());
        badge->setSize(kBadgeSize, kBadgeSize);
        badge->align(LV_ALIGN_CENTER, 0, 0);
        badge->setRadius(kBadgeSize / 2);
        badge->setBgOpa(LV_OPA_COVER);
        badge->setBorderWidth(1);
        badge->setPaddingAll(0);
        badge->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        badge->removeFlag(LV_OBJ_FLAG_CLICKABLE);
        style_macaron_badge(badge->get(), _macaron_swatches[swatch_idx], is_light);

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

void LauncherView::pollInput()
{
    if (_key_manager) {
        const auto ev = _key_manager->update(true);
        if (ev != input::KeyEvent::None) {
            _pending_key_event = ev;
        }
    }
}

bool LauncherView::hasPendingImmediateWork() const
{
    return _pending_key_event != input::KeyEvent::None || _clicked_app_id != -1;
}

void LauncherView::update()
{
    if (_pending_key_event != input::KeyEvent::None) {
        const auto ev      = _pending_key_event;
        _pending_key_event = input::KeyEvent::None;
        switch (ev) {
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
        int app_id      = _clicked_app_id;
        _clicked_app_id = -1;
        if (onAppClicked) {
            onAppClicked(app_id);
        }
        return;
    }

    if (GetHAL().getThemeMode() != _last_theme) {
        apply_theme();
    }

    if (_pager_scrolling) {
        return;
    }

    if (_clock_faces && isOnClockPage()) {
        _clock_faces->update();
    }

    uint32_t now = GetHAL().millis();
    if (now - _last_anim_tick >= 380) {
        _last_anim_tick = now;
        if (!isOnClockPage() && GetHAL().getWifiStatus().mode == Hal::WifiMode::Connecting) {
            _conn_anim_step = (_conn_anim_step + 1) % 3;
            update_status_info(false);
            _last_status_tick = now;
        }
    }
    if (now - _last_status_tick >= 1000) {
        _last_status_tick = now;
        update_status_info(false);
    }
}

void LauncherView::ensure_light_grad_lut()
{
    if (_light_grad_lut) {
        return;
    }
    constexpr size_t kLutBytes = kGradRowPhases * kGradLutLen * sizeof(uint16_t);
    // Allocate in internal SRAM so scanline memcpy bypasses external PSRAM cache completely
    // and keeps the 29 KB partial render buffer 100% hot in L1 D-Cache.
    _light_grad_lut = static_cast<uint16_t*>(heap_caps_malloc(kLutBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!_light_grad_lut) {
        _light_grad_lut = static_cast<uint16_t*>(heap_caps_malloc(kLutBytes, MALLOC_CAP_8BIT));
    }
    if (_light_grad_lut) {
        build_light_grad_lut(_light_grad_lut);
    }
}

void LauncherView::desktop_cover_check_cb(lv_event_t* e)
{
    auto* self = static_cast<LauncherView*>(lv_event_get_user_data(e));
    if (!self || self->_last_theme != Hal::ThemeMode::Light || !self->_light_grad_lut) {
        return;
    }
    lv_obj_t* obj              = lv_event_get_current_target_obj(e);
    const lv_area_t* cover_req = lv_event_get_cover_area(e);
    if (!obj || !cover_req || lv_obj_get_style_opa(obj, LV_PART_MAIN) < LV_OPA_MAX) {
        return;
    }
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    if (cover_req->x1 >= coords.x1 && cover_req->y1 >= coords.y1 && cover_req->x2 <= coords.x2 &&
        cover_req->y2 <= coords.y2) {
        // Leave info.res == LV_COVER_RES_COVER so LVGL skips redundant parent fills behind the gradient
        lv_event_stop_processing(e);
    } else {
        lv_event_set_cover_res(e, LV_COVER_RES_NOT_COVER);
        lv_event_stop_processing(e);
    }
}

void LauncherView::desktop_draw_main_cb(lv_event_t* e)
{
    auto* self = static_cast<LauncherView*>(lv_event_get_user_data(e));
    if (!self || self->_last_theme != Hal::ThemeMode::Light) {
        return;
    }
    self->ensure_light_grad_lut();
    if (!self->_light_grad_lut) {
        return;
    }

    lv_obj_t* obj     = lv_event_get_current_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    if (!obj || !layer) {
        return;
    }

    if (layer->draw_task_head) {
        lv_draw_dispatch();
    }
    if (!layer->draw_buf) {
        lv_draw_layer_alloc_buf(layer);
    }
    lv_draw_buf_t* draw_buf = layer->draw_buf;
    if (!draw_buf || !draw_buf->data) {
        return;
    }

    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);

    lv_area_t clipped;
    lv_area_t draw_area;
    if (!intersect_area(clipped, obj_coords, layer->_clip_area) ||
        !intersect_area(draw_area, clipped, layer->buf_area)) {
        return;
    }

    const int32_t w   = draw_area.x2 - draw_area.x1 + 1;
    const int32_t lx1 = std::clamp<int32_t>(draw_area.x1 - obj_coords.x1, 0, kGradMaxCoord);
    const int32_t bx  = draw_area.x1 - layer->buf_area.x1;
    const auto cf     = draw_buf->header.cf;

    for (int32_t sy = draw_area.y1; sy <= draw_area.y2; ++sy) {
        const int32_t ly = std::clamp<int32_t>(sy - obj_coords.y1, 0, kGradMaxCoord);
        int32_t k0       = (kGradMaxCoord - ly) + lx1;
        if (k0 + w > static_cast<int32_t>(kGradLutLen)) {
            k0 = static_cast<int32_t>(kGradLutLen) - w;
        }
        const uint16_t* src = &self->_light_grad_lut[(ly & 3) * kGradLutLen + k0];
        uint8_t* dst_row    = draw_buf->data + (sy - layer->buf_area.y1) * draw_buf->header.stride;

        if (cf == LV_COLOR_FORMAT_RGB565) {
            std::memcpy(reinterpret_cast<uint16_t*>(dst_row) + bx, src, static_cast<size_t>(w) * sizeof(uint16_t));
        } else if (cf == LV_COLOR_FORMAT_ARGB8888 || cf == LV_COLOR_FORMAT_XRGB8888) {
            uint32_t* dst32 = reinterpret_cast<uint32_t*>(dst_row) + bx;
            for (int32_t i = 0; i < w; ++i) {
                const uint16_t c = src[i];
                const uint32_t r = (((c >> 11) & 0x1F) * 527 + 23) >> 6;
                const uint32_t g = (((c >> 5) & 0x3F) * 259 + 33) >> 6;
                const uint32_t b = ((c & 0x1F) * 527 + 23) >> 6;
                dst32[i]         = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
    }
}

void LauncherView::apply_theme()
{
    const auto mode            = GetHAL().getThemeMode();
    const auto& theme          = GetHAL().getTheme();
    const bool is_light        = (mode == Hal::ThemeMode::Light);
    const uint32_t surface_col = is_light ? kLightDesktopSurface : theme.cardBackground;
    const uint32_t border_col  = is_light ? kLightDesktopBorder : theme.border;
    _last_theme                = mode;

    if (is_light) {
        ensure_light_grad_lut();
    }

    if (_panel) {
        _panel->setBgColor(lv_color_hex(theme.background));
    }
    if (_desktop_page) {
        _desktop_page->setBgColor(lv_color_hex(theme.background));
        _desktop_page->setBgOpa(is_light ? LV_OPA_TRANSP : LV_OPA_COVER);
        lv_obj_invalidate(_desktop_page->get());
    }
    if (_clock_faces) {
        _clock_faces->applyTheme();
    }
    if (_drag_handle) {
        _drag_handle->setBgColor(lv_color_hex(is_light ? kLightDragHandle : theme.textMuted));
    }
    if (_wifi_pill) {
        _wifi_pill->setBgColor(lv_color_hex(surface_col));
        _wifi_pill->setBorderColor(lv_color_hex(border_col));
    }
    if (_bat_pill) {
        _bat_pill->setBgColor(lv_color_hex(surface_col));
        _bat_pill->setBorderColor(lv_color_hex(border_col));
    }
    if (_bat_label) {
        _bat_label->setTextColor(lv_color_hex(theme.statusBarPrimary));
    }
    if (_bat_bar) {
        _bat_bar->setBgColor(lv_color_hex(surface_col));
        _bat_bar->setOutlineColor(lv_color_hex(theme.statusBarPrimary));
    }
    if (_bat_tip) {
        _bat_tip->setBgColor(lv_color_hex(theme.statusBarPrimary));
    }
    if (_bat_lightning) {
        _bat_lightning->setImageRecolor(lv_color_hex(theme.statusBarPrimary));
    }
    if (_orbit_hub) {
        _orbit_hub->setBgColor(lv_color_hex(surface_col));
        _orbit_hub->setBgOpa(LV_OPA_COVER);
        _orbit_hub->setBorderColor(lv_color_hex(border_col));
    }
    if (_orbit_time) {
        _orbit_time->setTextColor(lv_color_hex(theme.textPrimary));
    }
    for (size_t i = 0; i < _icon_badges.size(); ++i) {
        if (_icon_badges[i]) {
            const size_t swatch_idx =
                (i < _badge_swatch_indices.size()) ? _badge_swatch_indices[i] : (i % _macaron_swatches.size());
            style_macaron_badge(_icon_badges[i]->get(), _macaron_swatches[swatch_idx], is_light);
        }
    }
    update_status_info(true);
}

void LauncherView::update_status_info(bool force)
{
    const auto& theme      = GetHAL().getTheme();
    const bool is_light    = (GetHAL().getThemeMode() == Hal::ThemeMode::Light);
    const auto wifi_status = GetHAL().getWifiStatus();

    const int wifi_key = (static_cast<int>(wifi_status.mode) << 4) |
                         ((wifi_status.mode == Hal::WifiMode::Connecting) ? (_conn_anim_step & 0x0F) : 0);
    if ((force || wifi_key != _last_wifi_state_key) && _wifi_icon && _wifi_label) {
        _last_wifi_state_key = wifi_key;
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
    if ((force || t.hour != _last_orbit_hour || t.minute != _last_orbit_min) && _orbit_time) {
        _last_orbit_hour = t.hour;
        _last_orbit_min  = t.minute;
        _orbit_time->setText(fmt::format("{:02d}:{:02d}", t.hour, t.minute));
    }

    const uint8_t bat_level = GetHAL().getBatteryLevel();
    const bool charging     = GetHAL().isBatteryCharging();
    if (force || bat_level != _last_bat_level || charging != _last_bat_charging) {
        _last_bat_level    = bat_level;
        _last_bat_charging = charging;
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
}

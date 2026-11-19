/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <algorithm>
#include <cmath>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int kScreenSize   = 466;
constexpr int kScreenCenter = 233;
constexpr int kRowWidth     = 364;
constexpr int kRowHeight    = 78;
constexpr int kRowGap       = 14;
constexpr int kTopPad       = 136;
constexpr int kBottomPad    = 156;

}  // namespace

SelectMenuPage::SelectMenuPage(Config config) : _config(std::move(config))
{
    _root = std::make_unique<Container>(lv_screen_active());
    _root->setSize(kScreenSize, kScreenSize);
    _root->setBgColor(lv_color_hex(0x000000));
    _root->setBgOpa(LV_OPA_COVER);
    _root->setBorderWidth(0);
    _root->setPaddingAll(0);
    _root->setRadius(0);
    _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _list = std::make_unique<Container>(_root->get());
    _list->setSize(kScreenSize, kScreenSize);
    _list->align(LV_ALIGN_CENTER, 0, 0);
    _list->setBgColor(lv_color_hex(0x000000));
    _list->setBgOpa(LV_OPA_COVER);
    _list->setBorderWidth(0);
    _list->setPadding(0, 0, 0, 0);
    _list->setRadius(0);
    _list->setScrollDir(LV_DIR_VER);
    _list->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(_list->get(), LV_SCROLL_SNAP_CENTER);

    int cursor_y = kTopPad;
    _rows.reserve(_config.items.size());
    for (int i = 0; i < static_cast<int>(_config.items.size()); ++i) {
        create_item_row(cursor_y, _config.items[i], i);
        cursor_y += kRowHeight + kRowGap;
    }

    // Add bottom spacer so the last item can snap to center
    auto bottom_spacer = lv_obj_create(_list->get());
    lv_obj_set_size(bottom_spacer, 10, kBottomPad);
    lv_obj_set_pos(bottom_spacer, 0, cursor_y - kRowGap);
    lv_obj_set_style_bg_opa(bottom_spacer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(bottom_spacer, 0, LV_PART_MAIN);
    lv_obj_remove_flag(bottom_spacer, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(
        _list->get(),
        [](lv_event_t* e) {
            auto* self = static_cast<SelectMenuPage*>(lv_event_get_user_data(e));
            if (!self) {
                return;
            }
            const auto code = lv_event_get_code(e);
            if (code == LV_EVENT_SCROLL_BEGIN) {
                self->_is_scrolling        = true;
                self->_suppress_next_click = true;
            } else if (code == LV_EVENT_SCROLL_END) {
                self->_is_scrolling   = false;
                self->_scroll_end_ms  = GetHAL().millis();
                self->update_radial_effects();
            } else if (code == LV_EVENT_SCROLL) {
                self->update_radial_effects();
            }
        },
        LV_EVENT_ALL, this);

    // Subtle dark top header backdrop so curved controls & title stay crisp over scrolling rows
    _top_fade = std::make_unique<Container>(_root->get());
    _top_fade->setSize(kScreenSize, 104);
    _top_fade->align(LV_ALIGN_TOP_MID, 0, 0);
    _top_fade->setBorderWidth(0);
    _top_fade->setPaddingAll(0);
    _top_fade->setRadius(0);
    _top_fade->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _top_fade->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_top_fade->get(), lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(_top_fade->get(), lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(_top_fade->get(), LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_top_fade->get(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_stop(_top_fade->get(), 255, LV_PART_MAIN);
    lv_obj_set_style_bg_main_stop(_top_fade->get(), 170, LV_PART_MAIN);

    _action_bar = std::make_unique<ArcActionBar>(_root->get(), _config.leftActionText.c_str(), "", _config.title.c_str(),
                                                 _config.leftActionColor, 0x4AD78C);

    if (_config.initialScrollIndex > 0 && _config.initialScrollIndex < static_cast<int>(_rows.size())) {
        int target_scroll = _rows[_config.initialScrollIndex].centerY - kScreenCenter;
        if (target_scroll > 0) {
            lv_obj_scroll_to_y(_list->get(), target_scroll, LV_ANIM_OFF);
        }
    }

    update_radial_effects();
}

SelectMenuPage::~SelectMenuPage() = default;

void SelectMenuPage::update()
{
    if (_action_bar) {
        _action_bar->update();
        if (_action_bar->consumeLeftClicked()) {
            _pending_left_action = true;
        }
    }

    if (_pending_left_action) {
        _pending_left_action = false;
        if (_config.onLeftAction) {
            _config.onLeftAction();
        }
        return;
    }

    if (_pending_item_index >= 0) {
        int idx             = _pending_item_index;
        _pending_item_index = -1;
        if (idx < static_cast<int>(_config.items.size()) && _config.items[idx].onClick) {
            _config.items[idx].onClick();
        }
    }
}

int SelectMenuPage::currentCenterIndex() const
{
    if (_rows.empty() || !_list) {
        return 0;
    }
    const int scroll_y = static_cast<int>(lv_obj_get_scroll_y(_list->get()));
    const int center_y = scroll_y + kScreenCenter;
    int best_idx       = 0;
    int best_dist      = std::abs(_rows[0].centerY - center_y);
    for (int i = 1; i < static_cast<int>(_rows.size()); ++i) {
        int dist = std::abs(_rows[i].centerY - center_y);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx  = i;
        }
    }
    return best_idx;
}

void SelectMenuPage::create_item_row(int y, const MenuItem& item, int itemIdx)
{
    RowWidgets row;
    row.centerY = y + kRowHeight / 2;

    row.card = std::make_unique<Container>(_list->get());
    row.card->setSize(kRowWidth, kRowHeight);
    row.card->align(LV_ALIGN_TOP_MID, 0, y);
    row.card->setRadius(kRowHeight / 2);
    row.card->setBorderWidth(0);
    row.card->setShadowWidth(0);
    row.card->setPaddingAll(0);
    row.card->setBgColor(lv_color_hex(0x1B1F24));
    row.card->setBgOpa(LV_OPA_COVER);
    row.card->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    row.card->addFlag(LV_OBJ_FLAG_CLICKABLE);
    row.card->addFlag(LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    row.card->setTransformPivot(kRowWidth / 2, kRowHeight / 2);
    lv_obj_set_style_bg_color(row.card->get(), lv_color_hex(0x2A3038), LV_STATE_PRESSED);

    // Circular icon badge
    row.badge = std::make_unique<Container>(row.card->get());
    row.badge->setSize(46, 46);
    row.badge->align(LV_ALIGN_LEFT_MID, 16, 0);
    row.badge->setRadius(LV_RADIUS_CIRCLE);
    row.badge->setBorderWidth(0);
    row.badge->setShadowWidth(0);
    row.badge->setPaddingAll(0);
    row.badge->setBgColor(lv_color_hex(item.accentColor));
    row.badge->setBgOpa(LV_OPA_20);
    row.badge->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    row.badge->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    row.iconLabel = std::make_unique<Label>(row.badge->get());
    row.iconLabel->setText(item.icon ? item.icon : LV_SYMBOL_SETTINGS);
    row.iconLabel->setTextFont(&lv_font_montserrat_20);
    row.iconLabel->setTextColor(lv_color_hex(item.accentColor));
    row.iconLabel->align(LV_ALIGN_CENTER, 0, 0);
    row.iconLabel->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    // Text area
    const int text_x     = 76;
    const int text_width = item.showChevron ? 238 : 260;

    row.titleLabel = std::make_unique<Label>(row.card->get());
    row.titleLabel->setText(item.title);
    row.titleLabel->setTextFont(&lv_font_montserrat_22);
    row.titleLabel->setTextColor(lv_color_hex(0xFFFFFF));
    row.titleLabel->setWidth(text_width);
    row.titleLabel->setLongMode(LV_LABEL_LONG_MODE_DOTS);
    row.titleLabel->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    if (!item.subtitle.empty()) {
        row.titleLabel->align(LV_ALIGN_LEFT_MID, text_x, -12);

        row.subtitleLabel = std::make_unique<Label>(row.card->get());
        row.subtitleLabel->setText(item.subtitle);
        row.subtitleLabel->setTextFont(&lv_font_montserrat_16);
        row.subtitleLabel->setTextColor(lv_color_hex(0x8FA0B0));
        row.subtitleLabel->setWidth(text_width);
        row.subtitleLabel->setLongMode(LV_LABEL_LONG_MODE_DOTS);
        row.subtitleLabel->align(LV_ALIGN_LEFT_MID, text_x, 14);
        row.subtitleLabel->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    } else {
        row.titleLabel->align(LV_ALIGN_LEFT_MID, text_x, 0);
    }

    if (item.showChevron) {
        row.chevronLabel = std::make_unique<Label>(row.card->get());
        row.chevronLabel->setText(LV_SYMBOL_RIGHT);
        row.chevronLabel->setTextFont(&lv_font_montserrat_16);
        row.chevronLabel->setTextColor(lv_color_hex(0x5A6570));
        row.chevronLabel->align(LV_ALIGN_RIGHT_MID, -22, 0);
        row.chevronLabel->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    }

    if (item.onClick) {
        struct ClickCtx {
            SelectMenuPage* self;
            int index;
        };
        auto* ctx = new ClickCtx{this, itemIdx};
        lv_obj_add_event_cb(
            row.card->get(),
            [](lv_event_t* e) {
                auto* c = static_cast<ClickCtx*>(lv_event_get_user_data(e));
                if (!c || !c->self) {
                    return;
                }
                const auto code = lv_event_get_code(e);
                if (code == LV_EVENT_DELETE) {
                    delete c;
                    return;
                }
                auto* self = c->self;
                if (code == LV_EVENT_PRESSED) {
                    if (self->_is_scrolling || lv_obj_is_scrolling(self->_list->get())) {
                        self->_suppress_next_click = true;
                    } else {
                        self->_suppress_next_click = false;
                        if (auto* indev = lv_indev_active()) {
                            lv_indev_get_point(indev, &self->_press_point);
                        }
                    }
                } else if (code == LV_EVENT_CLICKED) {
                    if (self->_suppress_next_click || self->_is_scrolling) {
                        return;
                    }
                    if (GetHAL().millis() - self->_scroll_end_ms < 140) {
                        return;
                    }
                    if (auto* indev = lv_indev_active()) {
                        if (lv_indev_get_scroll_obj(indev) != nullptr) {
                            return;
                        }
                        lv_point_t pt = {0, 0};
                        lv_indev_get_point(indev, &pt);
                        if (std::abs(pt.x - self->_press_point.x) > 10 ||
                            std::abs(pt.y - self->_press_point.y) > 10) {
                            return;
                        }
                    }
                    self->_pending_item_index = c->index;
                }
            },
            LV_EVENT_ALL, ctx);
    }

    _rows.push_back(std::move(row));
}

void SelectMenuPage::update_radial_effects()
{
    if (!_list) {
        return;
    }
    const int scroll_y = static_cast<int>(lv_obj_get_scroll_y(_list->get()));
    for (auto& row : _rows) {
        if (!row.card) {
            continue;
        }
        const int screen_y = row.centerY - scroll_y;
        // Skip rows completely outside the visible viewport
        if (screen_y < -(kRowHeight) || screen_y > (kScreenSize + kRowHeight)) {
            continue;
        }

        const float dy = static_cast<float>(std::abs(screen_y - kScreenCenter));
        const float t  = std::min(dy / 195.0f, 1.0f);
        // Curve card width and opacity along the circular bezel without triggering
        // expensive LVGL ARGB8888 offscreen transform layers on every scroll frame.
        const int32_t target_w = (static_cast<int32_t>(kRowWidth - t * 48.0f) + 1) & ~1;
        const lv_opa_t opa     = static_cast<lv_opa_t>((static_cast<int>(255.0f - t * 115.0f)) & ~3);

        if (row.lastWidth != target_w) {
            row.lastWidth = target_w;
            lv_obj_set_width(row.card->get(), target_w);
        }
        if (row.lastOpa != opa) {
            row.lastOpa = opa;
            lv_obj_set_style_opa(row.card->get(), opa, LV_PART_MAIN);
        }
    }
}

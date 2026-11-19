/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "view/view.h"
#include "workers/workers.h"
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <cstdint>
#include <memory>

/**
 * @brief Derived App
 *
 */
class AppSetup : public mooncake::AppAbility {
public:
    AppSetup();

    // Override lifecycle callbacks
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class MenuPageId {
        Root,
        Display,
        Sound,
        DateTime,
        System,
    };

    std::unique_ptr<view::SelectMenuPage> _menu_page;
    std::unique_ptr<setup_workers::WorkerBase> _worker;
    std::unique_ptr<input::KeyManager> _key_manager;

    MenuPageId _current_page = MenuPageId::Root;
    MenuPageId _pending_page = MenuPageId::Root;
    int _root_scroll_index   = 0;
    int _sub_scroll_index    = 0;
    bool _switch_menu        = false;
    bool _destroy_menu       = false;
    bool _need_warm_reset    = false;

    void open_menu(MenuPageId page);
    view::SelectMenuPage::Config build_menu_config(MenuPageId page);
};

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "gemini_client.h"
#include "gemini_config_server.h"
#include "view/view.h"
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <memory>

class AppGemini : public mooncake::AppAbility {
public:
    AppGemini();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void toggleWebConfig();

    std::unique_ptr<view::GeminiView> _view;
    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<gemini::GeminiLiveClient> _client;
    std::unique_ptr<gemini::GeminiConfigServer> _web_server;
    bool _is_talking_active = false;
};

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_gemini.h"
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>

using namespace mooncake;

AppGemini::AppGemini()
{
    setAppInfo().name = "Gemini.AI";
    setAppInfo().icon = (void*)&icon_gemini;
}

void AppGemini::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppGemini::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _key_manager        = std::make_unique<input::KeyManager>();
    _client             = std::make_unique<gemini::GeminiLiveClient>();
    _web_server         = std::make_unique<gemini::GeminiConfigServer>();
    _btn_a_hold_talking = false;

    // Ensure Wi-Fi is enabled when opening Gemini Live
    if (!GetHAL().isWifiEnabled()) {
        GetHAL().setWifiEnabled(true, true);
    }

    {
        LvglLockGuard lock;
        _view = std::make_unique<view::GeminiView>();
        _view->init(lv_screen_active());

        _view->onTalkButtonClicked = [this]() {
            if (_web_server && _web_server->isRunning()) {
                return;
            }
            if (_client) {
                _client->togglePttTalking();
            }
        };

        _view->onConfigButtonClicked = [this]() { toggleWebConfig(); };
    }

    auto cfg = gemini::GeminiConfig::loadFromNvs();
    _client->start(cfg);
}

void AppGemini::toggleWebConfig()
{
    if (!_web_server || !_client) {
        return;
    }

    if (_web_server->isRunning()) {
        _web_server->stop();
        auto cfg = gemini::GeminiConfig::loadFromNvs();
        _client->start(cfg);
    } else {
        _client->stop();
        if (_web_server->start()) {
            LvglLockGuard lock;
            if (_view) {
                _view->setStatusText("Web Config Active");
                _view->setTranscriptText(
                    "", "Wi-Fi: " + _web_server->ssid() + "\nOpen: " + _web_server->pageUrl());
            }
        }
    }
}

void AppGemini::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }

    // Physical Button A: Hold to Talk (when Button B is not held for GoHome)
    if (_client && (!_web_server || !_web_server->isRunning())) {
        if (GetHAL().btnA.isHolding() && !GetHAL().btnB.isPressed()) {
            if (!_btn_a_hold_talking) {
                _btn_a_hold_talking = true;
                _client->setPttTalking(true);
            }
        } else if (_btn_a_hold_talking && GetHAL().btnA.isReleased()) {
            _btn_a_hold_talking = false;
            _client->setPttTalking(false);
        } else if (GetHAL().btnA.wasClicked() && !GetHAL().btnB.isPressed()) {
            _client->togglePttTalking();
        }
    }

    // If user saved config in Web UI, automatically reload and reconnect
    if (_web_server && _web_server->isRunning() && _web_server->consumeSavedFlag()) {
        _web_server->stop();
        auto cfg = gemini::GeminiConfig::loadFromNvs();
        _client->start(cfg);
    }

    LvglLockGuard lock;
    if (_view) {
        const bool web_active = _web_server && _web_server->isRunning();
        if (web_active) {
            _view->setState(gemini::SessionState::Ready, false, true);
            _view->setStatusText("Web Setup Mode");
            _view->setTranscriptText("", "Wi-Fi: " + _web_server->ssid() + "\n" + _web_server->pageUrl());
            _view->setAudioLevel(0.15f);
        } else if (_client) {
            auto st          = _client->getState();
            bool talking     = _client->isPttTalking();
            std::string stat = _client->getStatusText();
            std::string u_tx = _client->getUserTranscript();
            std::string m_tx = _client->getModelTranscript();

            _view->setState(st, talking, false);
            _view->setStatusText(stat);
            if (!u_tx.empty() || !m_tx.empty()) {
                _view->setTranscriptText(u_tx, m_tx);
            }
            _view->setAudioLevel(GetHAL().audioGetCurrentRms());
        }
        _view->update();
    }
}

void AppGemini::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_web_server) {
        _web_server->stop();
        _web_server.reset();
    }

    if (_client) {
        _client->stop();
        _client.reset();
    }

    _key_manager.reset();

    LvglLockGuard lock;
    _view.reset();
}

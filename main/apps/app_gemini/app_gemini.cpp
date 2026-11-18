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

    _key_manager       = std::make_unique<input::KeyManager>();
    _client            = std::make_unique<gemini::GeminiLiveClient>();
    _web_server        = std::make_unique<gemini::GeminiConfigServer>();
    _is_talking_active = false;

    // Ensure Wi-Fi is enabled when opening Gemini Live
    if (!GetHAL().isWifiEnabled()) {
        GetHAL().setWifiEnabled(true, true);
    }

    {
        LvglLockGuard lock;
        _view = std::make_unique<view::GeminiView>();
        _view->init(lv_screen_active());

        _view->onExitWebConfigClicked = [this]() {
            if (_web_server && _web_server->isRunning()) {
                toggleWebConfig();
            }
        };
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
                _view->setStatusText("Web Setup Mode");
                _view->setTranscriptText(
                    "", "Wi-Fi: " + _web_server->ssid() + "\nOpen: " + _web_server->pageUrl());
            }
        }
    }
}

void AppGemini::onRunning()
{
    // GoHome: KeyManager GoHome event OR simultaneous press of Button A + Button B
    const bool dual_pressed = GetHAL().btnA.isPressed() && GetHAL().btnB.isPressed();
    if ((_key_manager && _key_manager->update() == input::KeyEvent::GoHome) || dual_pressed) {
        close();
        return;
    }

    // Button B click: Toggle Web Config AP mode
    if (GetHAL().btnB.wasClicked() && !GetHAL().btnA.isPressed()) {
        toggleWebConfig();
        return;
    }

    // Hold Button A OR hold the center pattern to talk
    if (_client && (!_web_server || !_web_server->isRunning())) {
        const bool btn_a_pressed = GetHAL().btnA.isPressed() && !GetHAL().btnB.isPressed();
        const bool pattern_held  = _view && _view->isPatternPressed();
        const bool should_talk   = btn_a_pressed || pattern_held;

        if (should_talk) {
            if (!_is_talking_active) {
                _is_talking_active = true;
                GetHAL().vibrate(25, 70);
                _client->setPttTalking(true);
            }
        } else {
            if (_is_talking_active) {
                _is_talking_active = false;
                GetHAL().vibrate(20, 60);
                _client->setPttTalking(false);
            }
        }
    }

    // If user saved config in Web UI, automatically reload and reconnect
    if (_web_server && _web_server->isRunning() && _web_server->consumeSavedFlag()) {
        _web_server->stop();
        auto cfg = gemini::GeminiConfig::loadFromNvs();
        _client->start(cfg);
    }

    // Audio spectrum & volume updates
    GetHAL().updateAudioSpectrum();
    const auto spectrum = GetHAL().getAudioSpectrum();

    LvglLockGuard lock;
    if (_view) {
        _view->setSpectrum(spectrum.bands);
        _view->setPeakFrequencyHz(spectrum.peakFrequencyHz);

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

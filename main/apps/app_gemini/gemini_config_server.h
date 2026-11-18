/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "gemini_client.h"
#include <atomic>
#include <esp_http_server.h>
#include <esp_netif.h>
#include <string>

namespace gemini {

class GeminiConfigServer {
public:
    GeminiConfigServer();
    ~GeminiConfigServer();

    bool start();
    void stop();

    bool isRunning() const
    {
        return _running.load();
    }

    bool consumeSavedFlag()
    {
        return _config_saved.exchange(false);
    }

    std::string pageUrl() const
    {
        return _page_url;
    }

    std::string ssid() const
    {
        return _ssid;
    }

private:
    static esp_err_t handlePage(httpd_req_t* req);
    static esp_err_t handleGetConfig(httpd_req_t* req);
    static esp_err_t handleSaveConfig(httpd_req_t* req);

    httpd_handle_t _server = nullptr;
    void* _dns_server      = nullptr;
    esp_netif_t* _ap_netif = nullptr;
    bool _started_ap       = false;
    std::atomic<bool> _running{false};
    std::atomic<bool> _config_saved{false};
    std::string _ssid;
    std::string _page_url;
};

}  // namespace gemini

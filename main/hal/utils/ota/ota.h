/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <esp_err.h>
#include <esp_http_server.h>

namespace ota {

enum class State {
    Idle,
    Listening,
    Flashing,
    Success,
    Failed
};

struct Status {
    State state = State::Idle;
    int progress = 0;              // 0 - 100
    std::size_t bytesReceived = 0;
    std::size_t totalBytes = 0;
    std::string runningPartition;
    std::string targetPartition;
    std::string ipAddress;
    std::string ssid;
    std::string message;
};

using LogCallback = std::function<void(std::string_view)>;

class OtaManager {
public:
    static OtaManager& getInstance();

    bool start(LogCallback onLog = nullptr);
    void stop();

    bool isRunning() const;
    Status getStatus() const;

    esp_err_t registerHttpdHandlers(httpd_handle_t server);

    // HTTP Handler callbacks (for both internal server and external like config_ap)
    static esp_err_t handle_ota_page(httpd_req_t* req);
    static esp_err_t handle_ota_info(httpd_req_t* req);
    static esp_err_t handle_ota_status(httpd_req_t* req);
    static esp_err_t handle_ota_upload(httpd_req_t* req);

    void setStatus(State state, int progress, std::size_t received, std::size_t total, const std::string& msg);

private:
    OtaManager();
    ~OtaManager();

    void log(const std::string& msg);
    bool start_access_point();
    bool start_web_server();
    void stop_access_point();
    void stop_web_server();

    mutable std::recursive_mutex _mutex;
    Status _status;
    bool _is_running = false;
    bool _started_ap = false;
    LogCallback _on_log = nullptr;

    httpd_handle_t _server = nullptr;
    void* _ap_netif = nullptr;
    void* _dns_server = nullptr;
};

}  // namespace ota

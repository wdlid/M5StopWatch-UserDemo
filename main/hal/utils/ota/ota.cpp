/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ota.h"
#include "ota_html.h"
#include <hal/hal.h>
#include <apps/common/common.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <dns_server.h>
#include <esp_app_format.h>
#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <lwip/ip_addr.h>
#include <nvs_flash.h>

namespace ota {
namespace {

constexpr const char* _tag = "OTA-Manager";
constexpr const char* _ota_ap_prefix = "M5StopWatch-OTA";
constexpr std::size_t _chunk_buffer_size = 4096;

static void restart_timer_callback(void* /*arg*/)
{
    ESP_LOGI(_tag, "Rebooting into new firmware...");
    esp_restart();
}

static void schedule_delayed_restart(uint32_t delay_ms)
{
    const esp_timer_create_args_t timer_args = {
        .callback = &restart_timer_callback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ota_reboot",
        .skip_unhandled_events = false,
    };
    esp_timer_handle_t timer = nullptr;
    if (esp_timer_create(&timer_args, &timer) == ESP_OK) {
        esp_timer_start_once(timer, static_cast<uint64_t>(delay_ms) * 1000);
    } else {
        ESP_LOGW(_tag, "Failed to create restart timer, doing vTaskDelay fallback");
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        esp_restart();
    }
}

std::string make_ap_ssid()
{
    uint8_t mac[6] = {};
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (ret != ESP_OK) {
        return std::string(_ota_ap_prefix);
    }
    char ssid[32] = {};
    snprintf(ssid, sizeof(ssid), "%s-%02X%02X", _ota_ap_prefix, mac[4], mac[5]);
    return std::string(ssid);
}

}  // namespace

OtaManager& OtaManager::getInstance()
{
    static OtaManager instance;
    return instance;
}

OtaManager::OtaManager()
{
    _status.state = State::Idle;
    _status.progress = 0;
    _status.bytesReceived = 0;
    _status.totalBytes = 0;
    _status.message = "Idle";

    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running) {
        _status.runningPartition = running->label;
    }
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    if (next) {
        _status.targetPartition = next->label;
    }
}

OtaManager::~OtaManager()
{
    stop();
}

void OtaManager::log(const std::string& msg)
{
    ESP_LOGI(_tag, "%s", msg.c_str());
    if (_on_log) {
        _on_log(msg);
    }
}

bool OtaManager::isRunning() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _is_running;
}

Status OtaManager::getStatus() const
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _status;
}

void OtaManager::setStatus(State state, int progress, std::size_t received, std::size_t total, const std::string& msg)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _status.state = state;
    _status.progress = progress;
    _status.bytesReceived = received;
    _status.totalBytes = total;
    if (!msg.empty()) {
        _status.message = msg;
    }
}

bool OtaManager::start(LogCallback onLog)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_is_running) {
        return true;
    }

    _on_log = onLog;

    // Check running partition and target partition
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    _status.runningPartition = running ? running->label : "unknown";
    _status.targetPartition = next ? next->label : "none";

    log("Starting OTA service. Running: " + _status.runningPartition + ", Target: " + _status.targetPartition);

    // Check Wi-Fi state: if connected in Station mode with valid IP, use it!
    auto wifiStatus = GetHAL().getWifiStatus();
    if (wifiStatus.connected && !wifiStatus.ipAddress.empty()) {
        _status.ssid = wifiStatus.ssid;
        _status.ipAddress = wifiStatus.ipAddress;
        _started_ap = false;
        log("Using active Wi-Fi connection: " + _status.ssid + " (" + _status.ipAddress + ")");
    } else {
        // Start OTA SoftAP
        if (!start_access_point()) {
            setStatus(State::Failed, 0, 0, 0, "Failed to start Access Point");
            return false;
        }
        _started_ap = true;
    }

    // Start HTTP Web Server
    if (!start_web_server()) {
        setStatus(State::Failed, 0, 0, 0, "Failed to start HTTP server");
        if (_started_ap) {
            stop_access_point();
            _started_ap = false;
        }
        return false;
    }

    _is_running = true;
    setStatus(State::Listening, 0, 0, 0, "Ready - open browser at http://" + _status.ipAddress + "/ota");
    return true;
}

void OtaManager::stop()
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (!_is_running) {
        return;
    }

    log("Stopping OTA service");
    stop_web_server();

    if (_started_ap) {
        stop_access_point();
        _started_ap = false;
    }

    _is_running = false;
    if (_status.state != State::Success) {
        _status.state = State::Idle;
        _status.message = "Idle";
    }
}

bool OtaManager::start_access_point()
{
    GetHAL().wifi_enter_badge_ap();

    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap_netif) {
        ap_netif = esp_netif_create_default_wifi_ap();
    }
    if (!ap_netif) {
        log("Failed to create AP netif");
        return false;
    }
    _ap_netif = ap_netif;

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    auto dns = new DnsServer();
    dns->Start(ip_info.gw);
    _dns_server = dns;

    std::string ssid = make_ap_ssid();
    _status.ssid = ssid;
    _status.ipAddress = "192.168.4.1";

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char*>(wifi_config.ap.ssid), ssid.c_str(), sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = ssid.size();
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_start();

    log("Started OTA SoftAP: " + ssid + " at http://192.168.4.1/ota");
    return true;
}

void OtaManager::stop_access_point()
{
    if (_dns_server) {
        static_cast<DnsServer*>(_dns_server)->Stop();
        delete static_cast<DnsServer*>(_dns_server);
        _dns_server = nullptr;
    }

    esp_wifi_stop();

    if (_ap_netif) {
        esp_netif_destroy_default_wifi(static_cast<esp_netif_t*>(_ap_netif));
        _ap_netif = nullptr;
    }

    GetHAL().wifi_exit_badge_ap();
}

bool OtaManager::start_web_server()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 10240;

    esp_err_t ret = httpd_start(&_server, &config);
    if (ret != ESP_OK) {
        log("Failed to start http server: " + std::string(esp_err_to_name(ret)));
        return false;
    }

    registerHttpdHandlers(_server);

    // Wildcard redirect for captive portal or root
    httpd_uri_t root_redirect = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = [](httpd_req_t* req) -> esp_err_t {
            if (strcmp(req->uri, "/ota") == 0 || strcmp(req->uri, "/ota/info") == 0 ||
                strcmp(req->uri, "/ota/status") == 0) {
                return ESP_OK;
            }
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/ota");
            httpd_resp_send(req, nullptr, 0);
            return ESP_OK;
        },
        .user_ctx = this
    };
    httpd_register_uri_handler(_server, &root_redirect);

    return true;
}

void OtaManager::stop_web_server()
{
    if (_server != nullptr) {
        httpd_stop(_server);
        _server = nullptr;
    }
}

esp_err_t OtaManager::registerHttpdHandlers(httpd_handle_t server)
{
    if (!server) return ESP_ERR_INVALID_ARG;

    httpd_uri_t ota_page = {
        .uri = "/ota",
        .method = HTTP_GET,
        .handler = &OtaManager::handle_ota_page,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &ota_page);

    httpd_uri_t ota_info = {
        .uri = "/ota/info",
        .method = HTTP_GET,
        .handler = &OtaManager::handle_ota_info,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &ota_info);

    httpd_uri_t ota_status = {
        .uri = "/ota/status",
        .method = HTTP_GET,
        .handler = &OtaManager::handle_ota_status,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &ota_status);

    httpd_uri_t ota_upload = {
        .uri = "/ota/upload",
        .method = HTTP_POST,
        .handler = &OtaManager::handle_ota_upload,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &ota_upload);

    return ESP_OK;
}

esp_err_t OtaManager::handle_ota_page(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    return httpd_resp_send(req, OTA_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t OtaManager::handle_ota_info(httpd_req_t* req)
{
    auto& mgr = OtaManager::getInstance();
    auto status = mgr.getStatus();

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"version\":\"%s\",\"running_partition\":\"%s\",\"target_partition\":\"%s\","
             "\"free_heap\":%u,\"min_free_heap\":%u}",
             common::FirmwareVersion.data(),
             status.runningPartition.c_str(),
             status.targetPartition.c_str(),
             static_cast<unsigned int>(esp_get_free_heap_size()),
             static_cast<unsigned int>(esp_get_minimum_free_heap_size()));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, buf);
}

esp_err_t OtaManager::handle_ota_status(httpd_req_t* req)
{
    auto& mgr = OtaManager::getInstance();
    auto status = mgr.getStatus();

    const char* state_str = "idle";
    switch (status.state) {
    case State::Idle: state_str = "idle"; break;
    case State::Listening: state_str = "ready"; break;
    case State::Flashing: state_str = "flashing"; break;
    case State::Success: state_str = "success"; break;
    case State::Failed: state_str = "failed"; break;
    }

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"state\":\"%s\",\"progress\":%d,\"bytes\":%zu,\"total\":%zu,\"message\":\"%s\"}",
             state_str, status.progress, status.bytesReceived, status.totalBytes, status.message.c_str());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_sendstr(req, buf);
}

esp_err_t OtaManager::handle_ota_upload(httpd_req_t* req)
{
    auto& mgr = OtaManager::getInstance();

    ESP_LOGI(_tag, "Starting firmware upload, content length: %zu bytes", static_cast<size_t>(req->content_len));

    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty payload or missing Content-Length");
        return ESP_FAIL;
    }

    constexpr const char* UPDATE_PATH = "/spiflash/update.bin";
    FILE* f = fopen(UPDATE_PATH, "wb");
    if (!f) {
        ESP_LOGE(_tag, "Failed to open %s for writing", UPDATE_PATH);
        mgr.setStatus(State::Failed, 0, 0, req->content_len, "Storage write failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage unavailable");
        return ESP_FAIL;
    }

    mgr.setStatus(State::Flashing, 0, 0, req->content_len, "Receiving firmware...");

    auto buffer = std::make_unique<char[]>(_chunk_buffer_size);
    if (!buffer) {
        ESP_LOGE(_tag, "Failed to allocate OTA buffer");
        fclose(f);
        remove(UPDATE_PATH);
        mgr.setStatus(State::Failed, 0, 0, req->content_len, "Out of memory");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    size_t received_total = 0;
    size_t last_yield_bytes = 0;
    int last_percent = -1;
    bool magic_checked = false;

    while (remaining > 0) {
        int to_read = (remaining < static_cast<int>(_chunk_buffer_size)) ? remaining : static_cast<int>(_chunk_buffer_size);
        int received = httpd_req_recv(req, buffer.get(), to_read);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGE(_tag, "Receive error: %d", received);
            fclose(f);
            remove(UPDATE_PATH);
            mgr.setStatus(State::Failed, 0, received_total, req->content_len, "Network timeout / read error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connection broken during upload");
            return ESP_FAIL;
        }

        // Validate ESP32 image magic byte (0xE9) in first packet
        if (!magic_checked) {
            if (received > 0 && static_cast<uint8_t>(buffer[0]) != ESP_IMAGE_HEADER_MAGIC) {
                ESP_LOGE(_tag, "Invalid binary magic: 0x%02X (expected 0x%02X)",
                         static_cast<uint8_t>(buffer[0]), ESP_IMAGE_HEADER_MAGIC);
                fclose(f);
                remove(UPDATE_PATH);
                mgr.setStatus(State::Failed, 0, 0, req->content_len, "Invalid firmware file (not an ESP32 binary)");
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware magic header");
                return ESP_FAIL;
            }
            magic_checked = true;
        }

        size_t written = fwrite(buffer.get(), 1, received, f);
        if (written != static_cast<size_t>(received)) {
            ESP_LOGE(_tag, "Storage write failed");
            fclose(f);
            remove(UPDATE_PATH);
            mgr.setStatus(State::Failed, 0, received_total, req->content_len, "Storage write error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Storage write failed");
            return ESP_FAIL;
        }

        received_total += received;
        remaining -= received;

        int percent = static_cast<int>((received_total * 100) / req->content_len);
        if (percent != last_percent || remaining == 0) {
            last_percent = percent;
            mgr.setStatus(State::Flashing, percent, received_total, req->content_len, "Uploading...");
        }

        // Yield briefly every 32KB to let the network stack and UI tasks run smoothly
        if ((received_total - last_yield_bytes) >= 32768) {
            last_yield_bytes = received_total;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    fclose(f);

    const esp_partition_t* ota_1 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (!ota_1) {
        ESP_LOGE(_tag, "Recovery partition (ota_1) not found");
        remove(UPDATE_PATH);
        mgr.setStatus(State::Failed, 0, received_total, req->content_len, "Recovery partition missing");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recovery partition missing");
        return ESP_FAIL;
    }

    esp_err_t err = esp_ota_set_boot_partition(ota_1);
    if (err != ESP_OK) {
        ESP_LOGE(_tag, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        remove(UPDATE_PATH);
        mgr.setStatus(State::Failed, 0, received_total, req->content_len, "Set boot partition failed");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set boot partition");
        return ESP_FAIL;
    }

    ESP_LOGI(_tag, "Firmware staged in storage. Boot partition set to ota_1 (Recovery). Scheduling reboot...");
    mgr.setStatus(State::Success, 100, received_total, req->content_len, "Upload complete! Rebooting to flash...");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Firmware uploaded! Device is restarting to install...\"}");

    schedule_delayed_restart(1500);
    return ESP_OK;
}

}  // namespace ota

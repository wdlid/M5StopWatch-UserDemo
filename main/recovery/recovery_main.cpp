/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_vfs_fat.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_AMOLED.hpp>
#include <M5IOE1.h>
#include <M5PM1.h>

namespace {

static const char* TAG              = "Recovery";
static const char* UPDATE_FILE_PATH = "/spiflash/update.bin";

/* -------------------------------------------------------------------------- */
/*                               AMOLED Display                               */
/* -------------------------------------------------------------------------- */
static constexpr gpio_num_t cfg_pin_sclk = GPIO_NUM_40;
static constexpr gpio_num_t cfg_pin_io0  = GPIO_NUM_41;
static constexpr gpio_num_t cfg_pin_io1  = GPIO_NUM_42;
static constexpr gpio_num_t cfg_pin_io2  = GPIO_NUM_46;
static constexpr gpio_num_t cfg_pin_io3  = GPIO_NUM_45;
static constexpr gpio_num_t cfg_pin_cs   = GPIO_NUM_39;
static constexpr gpio_num_t cfg_pin_rst  = GPIO_NUM_NC;

class Panel_CO5300 : public lgfx::Panel_AMOLED {
public:
    Panel_CO5300()
    {
        _cfg.memory_width  = _cfg.panel_width = 480;
        _cfg.memory_height = _cfg.panel_height = 480;
        _write_depth       = lgfx::color_depth_t::rgb565_2Byte;
        _read_depth        = lgfx::color_depth_t::rgb565_2Byte;
    }

    const uint8_t* getInitCommands(uint8_t listno) const override
    {
        static constexpr uint8_t list0[] = {
            0x11, 0 + CMD_INIT_DELAY, 150,
            0xC4, 1, 0x80, 0x35, 1, 0x80,
            0x44, 2, 0x01, 0xD2,
            0x53, 1, 0x20, 0x20, 0, 0x36, 1, 0,
            0x51, 1, 0xA0, 0x29, 0, 0xff, 0xff
        };
        return (listno == 0) ? list0 : nullptr;
    }
};

class M5StopWatchDisplay : public M5GFX {
    lgfx::Bus_SPI _bus_instance;
    Panel_CO5300 _panel_instance;

public:
    M5StopWatchDisplay()
    {
        {
            auto cfg        = _bus_instance.config();
            cfg.freq_write  = 80000000;
            cfg.freq_read   = 10000000;
            cfg.pin_sclk    = cfg_pin_sclk;
            cfg.pin_io0     = cfg_pin_io0;
            cfg.pin_io1     = cfg_pin_io1;
            cfg.pin_io2     = cfg_pin_io2;
            cfg.pin_io3     = cfg_pin_io3;
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.spi_3wire   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg         = _panel_instance.config();
            cfg.pin_rst      = cfg_pin_rst;
            cfg.pin_cs       = cfg_pin_cs;
            cfg.panel_width  = 468;
            cfg.panel_height = 466;
            cfg.offset_x     = 6;
            cfg.offset_y     = 0;
            cfg.readable     = false;
            _panel_instance.config(cfg);
        }
        setPanel(&_panel_instance);
    }
};

static M5StopWatchDisplay s_display;
static std::unique_ptr<M5PM1> s_pmic;
static std::unique_ptr<M5IOE1> s_ioe;
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

void draw_ui_progress(const char* title, int progress, const char* detail = nullptr)
{
    s_display.fillScreen(TFT_BLACK);
    s_display.setTextDatum(textdatum_t::middle_center);

    s_display.setTextColor(TFT_WHITE, TFT_BLACK);
    s_display.setTextSize(2);
    s_display.drawString(title, 233, 170);

    constexpr int bar_x = 68;
    constexpr int bar_y = 220;
    constexpr int bar_w = 330;
    constexpr int bar_h = 24;
    s_display.drawRect(bar_x, bar_y, bar_w, bar_h, 0x39E7);  // Dark gray
    int fill_w = (bar_w - 4) * std::clamp(progress, 0, 100) / 100;
    if (fill_w > 0) {
        s_display.fillRect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, 0x4EB1);  // Green
    }

    char pct_str[16];
    snprintf(pct_str, sizeof(pct_str), "%d %%", progress);
    s_display.setTextColor(0x4EB1, TFT_BLACK);
    s_display.drawString(pct_str, 233, 270);

    if (detail) {
        s_display.setTextSize(1);
        s_display.setTextColor(0x9CD3, TFT_BLACK);
        s_display.drawString(detail, 233, 310);
    }
}

void draw_ui_ap_info(const char* ssid, const char* url)
{
    s_display.fillScreen(TFT_BLACK);
    s_display.setTextDatum(textdatum_t::middle_center);

    s_display.setTextColor(0xF9C6, TFT_BLACK);  // Coral/Orange
    s_display.setTextSize(2);
    s_display.drawString("RECOVERY MODE", 233, 140);

    s_display.setTextColor(TFT_WHITE, TFT_BLACK);
    s_display.setTextSize(2);
    s_display.drawString("Connect Wi-Fi:", 233, 195);

    s_display.setTextColor(0x4EB1, TFT_BLACK);  // Green
    s_display.setTextSize(2);
    s_display.drawString(ssid, 233, 230);

    s_display.setTextColor(0x9CD3, TFT_BLACK);  // Light blue/gray
    s_display.setTextSize(1);
    s_display.drawString("Open in browser:", 233, 280);

    s_display.setTextColor(TFT_WHITE, TFT_BLACK);
    s_display.setTextSize(2);
    s_display.drawString(url, 233, 310);
}

/* -------------------------------------------------------------------------- */
/*                               Hardware Init                                */
/* -------------------------------------------------------------------------- */
bool init_hardware()
{
    ESP_LOGI(TAG, "Initializing hardware...");

    // Initialize PMIC
    s_pmic = std::make_unique<M5PM1>();
    if (s_pmic->begin(I2C_NUM_0, M5PM1_DEFAULT_ADDR, 47, 48, M5PM1_I2C_FREQ_400K) == M5PM1_OK) {
        ESP_LOGI(TAG, "PMIC initialized");
        s_pmic->setI2cSleepTime(0);
        s_pmic->ldoSetPowerHold(true);
        s_pmic->setLedEnLevel(true);
    } else {
        ESP_LOGW(TAG, "PMIC init failed");
    }

    // Initialize IO Expander (to power up OLED reset pin and L3B_EN)
    s_ioe = std::make_unique<M5IOE1>();
    if (s_ioe->begin(I2C_NUM_0, 0x4F, 47, 48, M5IOE1_I2C_FREQ_400K) != M5IOE1_OK) {
        s_ioe->begin(I2C_NUM_0, 0x6F, 47, 48, M5IOE1_I2C_FREQ_400K);
    }
    if (s_ioe) {
        s_ioe->setI2cSleepTime(0);
        // OUTPUT = 0x03 in M5IOE1
        s_ioe->pinMode(M5IOE1_PIN_5, 0x03);  // OLED RESET
        s_ioe->digitalWrite(M5IOE1_PIN_5, 1);

        s_ioe->pinMode(M5IOE1_PIN_8, 0x03);  // L3B_EN
        s_ioe->digitalWrite(M5IOE1_PIN_8, 1);
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    // Initialize AMOLED Display
    s_display.init();
    s_display.setBrightness(180);
    s_display.fillScreen(TFT_BLACK);

    return true;
}

bool mount_storage()
{
    esp_vfs_fat_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed     = false;
    mount_config.max_files                  = 4;
    mount_config.allocation_unit_size       = 4096;
    mount_config.use_one_fat                = false;

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl("/spiflash", "storage", &mount_config, &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount storage (%s)", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "Storage mounted at /spiflash");
    return true;
}

/* -------------------------------------------------------------------------- */
/*                            Flash Staged Firmware                           */
/* -------------------------------------------------------------------------- */
bool flash_staged_firmware()
{
    FILE* f = fopen(UPDATE_FILE_PATH, "rb");
    if (!f) {
        return false;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size < 100 * 1024) {
        ESP_LOGE(TAG, "Update file too small: %ld bytes", file_size);
        fclose(f);
        remove(UPDATE_FILE_PATH);
        return false;
    }

    uint8_t magic = 0;
    if (fread(&magic, 1, 1, f) != 1 || magic != 0xE9) {
        ESP_LOGE(TAG, "Invalid magic byte: 0x%02X (expected 0xE9)", magic);
        fclose(f);
        remove(UPDATE_FILE_PATH);
        return false;
    }
    fseek(f, 0, SEEK_SET);

    const esp_partition_t* ota_0 =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!ota_0) {
        ESP_LOGE(TAG, "ota_0 partition not found!");
        fclose(f);
        return false;
    }

    if (static_cast<size_t>(file_size) > ota_0->size) {
        ESP_LOGE(TAG, "File size (%ld) exceeds ota_0 partition size (%zu)", file_size, ota_0->size);
        fclose(f);
        remove(UPDATE_FILE_PATH);
        return false;
    }

    draw_ui_progress("Erasing Flash...", 0, "Please do not power off");

    size_t erase_size = (file_size + 4095) & ~4095;
    ESP_LOGI(TAG, "Erasing ota_0 (%zu bytes)...", erase_size);
    esp_err_t err = esp_partition_erase_range(ota_0, 0, erase_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erase failed: %s", esp_err_to_name(err));
        fclose(f);
        return false;
    }

    ESP_LOGI(TAG, "Writing firmware to ota_0...");
    std::vector<uint8_t> buffer(32768);
    size_t written_total = 0;
    int last_pct         = -1;

    while (written_total < static_cast<size_t>(file_size)) {
        size_t to_read = std::min(buffer.size(), static_cast<size_t>(file_size) - written_total);
        size_t r       = fread(buffer.data(), 1, to_read, f);
        if (r == 0) {
            break;
        }

        err = esp_partition_write(ota_0, written_total, buffer.data(), r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Write error at offset %zu: %s", written_total, esp_err_to_name(err));
            fclose(f);
            return false;
        }

        written_total += r;
        int pct = static_cast<int>(written_total * 100 / file_size);
        if (pct != last_pct) {
            last_pct = pct;
            draw_ui_progress("Installing Update...", pct, "Writing to internal flash...");
        }
    }

    fclose(f);
    remove(UPDATE_FILE_PATH);

    if (written_total != static_cast<size_t>(file_size)) {
        ESP_LOGE(TAG, "Incomplete write (%zu / %ld)", written_total, file_size);
        return false;
    }

    draw_ui_progress("Finalizing...", 100, "Setting boot partition...");
    err = esp_ota_set_boot_partition(ota_0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return false;
    }

    draw_ui_progress("Update Complete!", 100, "Restarting watch...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return true;
}

/* -------------------------------------------------------------------------- */
/*                            Emergency Web AP Mode                           */
/* -------------------------------------------------------------------------- */
static const char RECOVERY_HTML[] = R"rawliteral(<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>M5StopWatch - Recovery</title>
  <style>
    body { font-family: sans-serif; background: #0f172a; color: #f8fafc; display: flex; flex-direction: column; align-items: center; justify-content: center; min-height: 100vh; margin: 0; padding: 20px; box-sizing: border-box; }
    .card { background: #1e293b; padding: 28px; border-radius: 16px; width: 100%; max-width: 440px; box-shadow: 0 4px 20px rgba(0,0,0,0.5); text-align: center; }
    h1 { color: #f59e0b; margin-top: 0; font-size: 22px; }
    p { color: #94a3b8; font-size: 14px; margin-bottom: 24px; line-height: 1.5; }
    input[type=file] { margin-bottom: 20px; font-size: 14px; }
    button { background: #10b981; color: #fff; border: none; padding: 12px 24px; border-radius: 8px; font-size: 16px; font-weight: bold; cursor: pointer; width: 100%; }
    button:hover { background: #059669; }
    #status { margin-top: 18px; font-size: 14px; color: #38bdf8; }
  </style>
</head>
<body>
  <div class="card">
    <h1>M5StopWatch Recovery</h1>
    <p>Upload a valid firmware (.bin) file to flash into main application partition.</p>
    <input type="file" id="file" accept=".bin"><br>
    <button onclick="upload()">Flash Firmware</button>
    <div id="status"></div>
  </div>
  <script>
    function upload() {
      var file = document.getElementById('file').files[0];
      if (!file) return alert('Select file first');
      var st = document.getElementById('status');
      st.innerText = 'Uploading: 0%';
      var xhr = new XMLHttpRequest();
      xhr.open('POST', '/ota/upload', true);
      xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
          st.innerText = 'Uploading: ' + Math.round((e.loaded / e.total) * 100) + '%';
        }
      };
      xhr.onload = function() {
        if (xhr.status == 200) {
          st.innerText = 'Flashing complete! Watch is restarting...';
        } else {
          st.innerText = 'Flash failed: ' + xhr.responseText;
        }
      };
      xhr.onerror = function() { st.innerText = 'Connection error'; };
      xhr.send(file);
    }
  </script>
</body>
</html>)rawliteral";

static httpd_handle_t s_http_server = nullptr;

esp_err_t upload_handler(httpd_req_t* req)
{
    ESP_LOGI(TAG, "Direct Web OTA upload started (%zu bytes)", req->content_len);
    const esp_partition_t* ota_0 =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!ota_0 || req->content_len > ota_0->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid size or partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err               = esp_ota_begin(ota_0, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    std::vector<char> buffer(4096);
    int remaining         = req->content_len;
    size_t received_total = 0;

    while (remaining > 0) {
        int to_read = std::min(static_cast<int>(buffer.size()), remaining);
        int r       = httpd_req_recv(req, buffer.data(), to_read);
        if (r <= 0) {
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buffer.data(), r);
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write error");
            return ESP_FAIL;
        }

        received_total += r;
        remaining -= r;
        int pct = static_cast<int>(received_total * 100 / req->content_len);
        draw_ui_progress("Web Flashing...", pct, "Flashing via browser...");
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA verify failed");
        return ESP_FAIL;
    }

    esp_ota_set_boot_partition(ota_0);
    draw_ui_progress("Update Complete!", 100, "Rebooting...");
    httpd_resp_sendstr(req, "OK");

    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK;
}

void start_emergency_recovery_ap()
{
    ESP_LOGI(TAG, "Starting Emergency Recovery SoftAP...");
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[32] = {};
    snprintf(ssid, sizeof(ssid), "M5StopWatch-Recovery-%02X%02X", mac[4], mac[5]);

    wifi_config_t wifi_cfg = {};
    strncpy(reinterpret_cast<char*>(wifi_cfg.ap.ssid), ssid, sizeof(wifi_cfg.ap.ssid) - 1);
    wifi_cfg.ap.ssid_len       = strlen(ssid);
    wifi_cfg.ap.max_connection = 4;
    wifi_cfg.ap.authmode       = WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
    esp_wifi_start();

    // Start HTTP Server
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.stack_size     = 8192;
    if (httpd_start(&s_http_server, &http_cfg) == ESP_OK) {
        httpd_uri_t page_uri = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = [](httpd_req_t* r) -> esp_err_t {
                return httpd_resp_send(r, RECOVERY_HTML, HTTPD_RESP_USE_STRLEN);
            },
            .user_ctx = nullptr
        };
        httpd_register_uri_handler(s_http_server, &page_uri);

        httpd_uri_t ota_uri = {
            .uri      = "/ota",
            .method   = HTTP_GET,
            .handler  = [](httpd_req_t* r) -> esp_err_t {
                return httpd_resp_send(r, RECOVERY_HTML, HTTPD_RESP_USE_STRLEN);
            },
            .user_ctx = nullptr
        };
        httpd_register_uri_handler(s_http_server, &ota_uri);

        httpd_uri_t upload_uri = {
            .uri      = "/ota/upload",
            .method   = HTTP_POST,
            .handler  = &upload_handler,
            .user_ctx = nullptr
        };
        httpd_register_uri_handler(s_http_server, &upload_uri);
    }

    draw_ui_ap_info(ssid, "http://192.168.4.1/ota");
}

}  // namespace

extern "C" void app_main()
{
    ESP_LOGI(TAG, "=== M5StopWatch Recovery Starting ===");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();

    // Initialize Hardware (PMIC, IO Expander, Display)
    init_hardware();

    // Mount Storage
    if (mount_storage()) {
        // Check for staged update
        if (flash_staged_firmware()) {
            return;
        }
    }

    // Fallback: If no staged firmware found or mounting failed, enter Emergency Web AP mode
    start_emergency_recovery_ap();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

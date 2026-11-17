/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/config_ap/config_ap.h"

#include <assets/assets.h>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <esp_heap_caps.h>
#include <M5GFX.h>

#include <mooncake_log.h>

static const std::string_view _tag = "HAL-Badge";

namespace {

constexpr const char* _badge_dir            = "/spiflash/badge";
constexpr const char* _active_slot_file     = "/spiflash/badge/active_slot.txt";
constexpr const char* _active_slots_file    = "/spiflash/badge/active_slots.txt";
constexpr const char* _interval_file        = "/spiflash/badge/interval.txt";
constexpr std::size_t _max_badge_slot_count = 6;

std::string slot_meta_path(std::size_t slot)
{
    char path[96] = {};
    snprintf(path, sizeof(path), "%s/slot_%u.meta", _badge_dir, static_cast<unsigned>(slot));
    return std::string(path);
}

std::string slot_image_path(std::size_t slot, std::string_view extension)
{
    char path[96] = {};
    snprintf(path, sizeof(path), "%s/slot_%u.%.*s", _badge_dir, static_cast<unsigned>(slot),
             static_cast<int>(extension.size()), extension.data());
    return std::string(path);
}

std::string slot_raw_path(std::size_t slot)
{
    char path[96] = {};
    snprintf(path, sizeof(path), "%s/slot_%u.raw", _badge_dir, static_cast<unsigned>(slot));
    return std::string(path);
}

bool ensure_badge_dir()
{
    if (mkdir(_badge_dir, 0775) == 0 || errno == EEXIST) {
        return true;
    }

    mclog::tagError(_tag, "failed to create badge dir: {}, errno={}", _badge_dir, errno);
    return false;
}

bool write_text_file(const std::string& path, std::string_view text)
{
    FILE* file = fopen(path.c_str(), "wb");
    if (file == nullptr) {
        mclog::tagError(_tag, "failed to open file for write: {}", path);
        return false;
    }

    const size_t written = fwrite(text.data(), 1, text.size(), file);
    fclose(file);
    if (written != text.size()) {
        mclog::tagError(_tag, "failed to write file: {}", path);
        return false;
    }

    return true;
}

bool write_binary_file(const std::string& path, const std::vector<uint8_t>& data)
{
    FILE* file = fopen(path.c_str(), "wb");
    if (file == nullptr) {
        mclog::tagError(_tag, "failed to open file for write: {}", path);
        return false;
    }

    const size_t written = data.empty() ? 0 : fwrite(data.data(), 1, data.size(), file);
    fclose(file);
    if (written != data.size()) {
        mclog::tagError(_tag, "failed to write file: {}", path);
        return false;
    }

    return true;
}

std::string read_text_file(const std::string& path)
{
    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return {};
    }

    char buffer[32]   = {};
    const size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);

    std::string text(buffer, size);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

std::string normalize_extension(std::string file_name, std::string content_type)
{
    auto normalize = [](std::string& text) {
        for (char& ch : text) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
    };

    normalize(file_name);
    normalize(content_type);

    const auto dot = file_name.find_last_of('.');
    if (dot != std::string::npos) {
        const std::string ext = file_name.substr(dot + 1);
        if (ext == "png" || ext == "jpg" || ext == "jpeg") {
            return ext == "jpeg" ? "jpg" : ext;
        }
    }

    if (content_type.find("png") != std::string::npos) {
        return "png";
    }
    if (content_type.find("jpeg") != std::string::npos || content_type.find("jpg") != std::string::npos) {
        return "jpg";
    }

    return {};
}

std::size_t read_active_slot()
{
    const std::string text = read_text_file(_active_slot_file);
    if (text.empty()) {
        return 0;
    }

    const unsigned long slot = strtoul(text.c_str(), nullptr, 10);
    if (slot >= _max_badge_slot_count) {
        mclog::tagWarn(_tag, "invalid active badge slot: {}", slot);
        return 0;
    }

    return static_cast<std::size_t>(slot);
}

std::string read_slot_extension(std::size_t slot)
{
    std::string ext = read_text_file(slot_meta_path(slot));
    if (ext == "png" || ext == "jpg") {
        return ext;
    }

    for (const auto* candidate : {"png", "jpg", "jpeg"}) {
        const std::string path = slot_image_path(slot, candidate);
        if (access(path.c_str(), R_OK) == 0) {
            return std::string(strcmp(candidate, "jpeg") == 0 ? "jpg" : candidate);
        }
    }

    return {};
}

std::string slot_content_type(std::string_view extension)
{
    if (extension == "jpg" || extension == "jpeg") {
        return "image/jpeg";
    }
    if (extension == "png") {
        return "image/png";
    }
    return "application/octet-stream";
}

bool read_binary_file(const std::string& path, std::vector<uint8_t>& data)
{
    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return false;
    }
    rewind(file);

    data.resize(static_cast<std::size_t>(size));
    const size_t read_size = data.empty() ? 0 : fread(data.data(), 1, data.size(), file);
    fclose(file);
    return read_size == data.size();
}

bool is_slot_available(std::size_t slot)
{
    return !read_slot_extension(slot).empty();
}

bool persist_active_slot(std::size_t slot)
{
    if (slot >= _max_badge_slot_count) {
        return false;
    }

    return write_text_file(_active_slot_file, std::to_string(slot));
}

bool clear_active_slot()
{
    if (unlink(_active_slot_file) == 0 || errno == ENOENT) {
        return true;
    }
    return false;
}

std::vector<bool> read_active_slots()
{
    std::vector<bool> active(_max_badge_slot_count, false);
    std::string text = read_text_file(_active_slots_file);
    if (!text.empty()) {
        char buf[64] = {};
        strncpy(buf, text.c_str(), sizeof(buf) - 1);
        char* token = strtok(buf, ", \t\r\n");
        while (token != nullptr) {
            unsigned long s = strtoul(token, nullptr, 10);
            if (s < _max_badge_slot_count) {
                active[s] = true;
            }
            token = strtok(nullptr, ", \t\r\n");
        }
    } else {
        std::size_t legacy = read_active_slot();
        if (legacy < _max_badge_slot_count) {
            active[legacy] = true;
        }
    }
    return active;
}

bool persist_active_slots(const std::vector<bool>& active)
{
    std::string s;
    for (std::size_t i = 0; i < active.size() && i < _max_badge_slot_count; ++i) {
        if (active[i]) {
            if (!s.empty()) s += ",";
            s += std::to_string(i);
        }
    }
    write_text_file(_active_slots_file, s);
    for (std::size_t i = 0; i < active.size() && i < _max_badge_slot_count; ++i) {
        if (active[i]) {
            persist_active_slot(i);
            break;
        }
    }
    return true;
}

uint32_t read_carousel_interval()
{
    std::string text = read_text_file(_interval_file);
    if (text.empty()) {
        return 5;
    }
    return static_cast<uint32_t>(strtoul(text.c_str(), nullptr, 10));
}

bool persist_carousel_interval(uint32_t seconds)
{
    return write_text_file(_interval_file, std::to_string(seconds));
}

std::size_t find_first_available_slot()
{
    for (std::size_t slot = 0; slot < _max_badge_slot_count; ++slot) {
        if (is_slot_available(slot)) {
            return slot;
        }
    }

    return _max_badge_slot_count;
}

std::size_t find_available_slot(std::size_t current_slot, int direction)
{
    if (direction == 0) {
        return _max_badge_slot_count;
    }

    for (std::size_t step = 1; step <= _max_badge_slot_count; ++step) {
        const std::size_t slot =
            static_cast<std::size_t>((static_cast<int>(current_slot) + direction * static_cast<int>(step) +
                                      static_cast<int>(_max_badge_slot_count)) %
                                     static_cast<int>(_max_badge_slot_count));
        if (is_slot_available(slot)) {
            return slot;
        }
    }

    return _max_badge_slot_count;
}

static uint8_t* _badge_rgb565_buf = nullptr;
static lv_image_dsc_t _badge_img_dsc = {};
static std::size_t _current_loaded_slot = 0;

static bool ensure_badge_buffer()
{
    if (_badge_rgb565_buf != nullptr) {
        return true;
    }
    _badge_rgb565_buf = (uint8_t*)heap_caps_malloc(466 * 466 * 2, MALLOC_CAP_SPIRAM);
    if (_badge_rgb565_buf == nullptr) {
        mclog::tagError(_tag, "failed to allocate badge PSRAM buffer");
        return false;
    }
    _badge_img_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    _badge_img_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    _badge_img_dsc.header.flags  = 0;
    _badge_img_dsc.header.w      = 466;
    _badge_img_dsc.header.h      = 466;
    _badge_img_dsc.header.stride = 466 * 2;
    _badge_img_dsc.data_size     = 466 * 466 * 2;
    _badge_img_dsc.data          = _badge_rgb565_buf;
    return true;
}

static bool convert_jpg_to_raw(const std::string& jpg_path, const std::string& raw_path, uint8_t* out_buf = nullptr)
{
    std::vector<uint8_t> jpg_data;
    if (!read_binary_file(jpg_path, jpg_data)) {
        return false;
    }

    LGFX_Sprite sprite;
    sprite.setColorDepth(16);
    sprite.setPsram(true);
    if (!sprite.createSprite(466, 466)) {
        return false;
    }

    sprite.drawJpg(jpg_data.data(), jpg_data.size(), 0, 0);
    const void* buf = sprite.getBuffer();
    if (buf == nullptr) {
        sprite.deleteSprite();
        return false;
    }

    if (out_buf != nullptr) {
        memcpy(out_buf, buf, 466 * 466 * 2);
    }

    FILE* f = fopen(raw_path.c_str(), "wb");
    if (f != nullptr) {
        fwrite(buf, 1, 466 * 466 * 2, f);
        fclose(f);
    }

    sprite.deleteSprite();
    return true;
}

bool load_badge_slot(lv_obj_t* image, std::size_t slot)
{
    if (image == nullptr || slot >= _max_badge_slot_count) {
        return false;
    }

    if (!ensure_badge_buffer()) {
        return false;
    }

    const std::string raw_path = slot_raw_path(slot);
    bool loaded = false;

    // 1. Fast path: load pre-decoded RGB565 from raw file (~15ms)
    FILE* f = fopen(raw_path.c_str(), "rb");
    if (f != nullptr) {
        size_t n = fread(_badge_rgb565_buf, 1, 466 * 466 * 2, f);
        fclose(f);
        if (n == 466 * 466 * 2) {
            loaded = true;
        }
    }

    // 2. Fallback: decode JPG on first load and cache as .raw
    if (!loaded) {
        const std::string ext = read_slot_extension(slot);
        if (!ext.empty()) {
            const std::string jpg_path = slot_image_path(slot, ext);
            if (convert_jpg_to_raw(jpg_path, raw_path, _badge_rgb565_buf)) {
                loaded = true;
            }
        }
    }

    if (!loaded) {
        return false;
    }

    _current_loaded_slot = slot;
    lv_image_set_src(image, &_badge_img_dsc);
    lv_obj_invalidate(image);
    persist_active_slot(slot);
    mclog::tagInfo(_tag, "instant loaded badge image, slot={}", slot);
    return true;
}

std::size_t find_next_active_slot(std::size_t current_slot)
{
    auto active = read_active_slots();
    std::size_t active_count = 0;
    for (std::size_t s = 0; s < _max_badge_slot_count; ++s) {
        if (active[s] && is_slot_available(s)) {
            active_count++;
        }
    }

    if (active_count == 0) {
        return find_available_slot(current_slot, 1);
    }

    for (std::size_t step = 1; step <= _max_badge_slot_count; ++step) {
        std::size_t next = (current_slot + step) % _max_badge_slot_count;
        if (active[next] && is_slot_available(next)) {
            return next;
        }
    }
    return current_slot;
}

std::size_t find_prev_active_slot(std::size_t current_slot)
{
    auto active = read_active_slots();
    std::size_t active_count = 0;
    for (std::size_t s = 0; s < _max_badge_slot_count; ++s) {
        if (active[s] && is_slot_available(s)) {
            active_count++;
        }
    }

    if (active_count == 0) {
        return find_available_slot(current_slot, -1);
    }

    for (std::size_t step = 1; step <= _max_badge_slot_count; ++step) {
        std::size_t prev = (current_slot + _max_badge_slot_count - step) % _max_badge_slot_count;
        if (active[prev] && is_slot_available(prev)) {
            return prev;
        }
    }
    return current_slot;
}

badge::config_ap::BadgeState make_badge_state()
{
    badge::config_ap::BadgeState state;
    state.slotCount           = _max_badge_slot_count;
    state.activeSlot          = read_active_slot();
    state.carouselIntervalSec = read_carousel_interval();
    state.slots.reserve(_max_badge_slot_count);

    auto active_slots = read_active_slots();

    for (std::size_t slot = 0; slot < _max_badge_slot_count; ++slot) {
        state.slots.push_back({
            .slot     = slot,
            .hasImage = is_slot_available(slot),
            .isActive = active_slots[slot],
        });
    }

    return state;
}

bool get_badge_image(std::size_t slot, badge::config_ap::ImageData& image, std::string& message)
{
    if (slot >= _max_badge_slot_count) {
        message = "invalid badge slot";
        return false;
    }

    const std::string extension = read_slot_extension(slot);
    if (extension.empty()) {
        message = "badge image not found";
        return false;
    }

    const std::string path = slot_image_path(slot, extension);
    if (!read_binary_file(path, image.data)) {
        message = "failed to read image file";
        return false;
    }

    image.contentType = slot_content_type(extension);
    return true;
}

bool set_active_badge_slot(std::size_t slot, bool active, std::string& message)
{
    if (slot >= _max_badge_slot_count) {
        message = "invalid badge slot";
        return false;
    }

    if (active && !is_slot_available(slot)) {
        message = "cannot activate empty slot";
        return false;
    }

    auto active_slots = read_active_slots();
    active_slots[slot] = active;
    persist_active_slots(active_slots);

    message = active ? ("slot " + std::to_string(slot + 1) + " activated")
                     : ("slot " + std::to_string(slot + 1) + " deactivated");
    return true;
}

bool set_carousel_interval(uint32_t seconds, std::string& message)
{
    persist_carousel_interval(seconds);
    message = "carousel interval updated to " + std::to_string(seconds) + "s";
    return true;
}

bool delete_badge_slot(std::size_t slot, std::string& message)
{
    if (slot >= _max_badge_slot_count) {
        message = "invalid badge slot";
        return false;
    }

    bool deleted = false;
    for (const auto* candidate : {"png", "jpg", "jpeg"}) {
        const std::string path = slot_image_path(slot, candidate);
        if (unlink(path.c_str()) == 0) {
            deleted = true;
        }
    }

    unlink(slot_raw_path(slot).c_str());

    const std::string meta_path = slot_meta_path(slot);
    if (unlink(meta_path.c_str()) == 0) {
        deleted = true;
    }

    if (!deleted) {
        message = "badge image not found";
        return false;
    }

    auto active = read_active_slots();
    if (slot < active.size()) {
        active[slot] = false;
        persist_active_slots(active);
    }

    if (read_active_slot() == slot) {
        const std::size_t next_slot = find_first_available_slot();
        if (next_slot < _max_badge_slot_count) {
            persist_active_slot(next_slot);
        } else {
            clear_active_slot();
        }
    }

    message = "badge image deleted";
    return true;
}

bool save_badge_upload(const badge::config_ap::UploadRequest& request, std::string& message)
{
    if (request.slot >= _max_badge_slot_count) {
        message = "invalid badge slot";
        return false;
    }
    if (request.data.empty()) {
        message = "empty upload body";
        return false;
    }
    if (!ensure_badge_dir()) {
        message = "failed to create badge directory";
        return false;
    }

    const std::string extension = normalize_extension(request.fileName, request.contentType);
    if (extension != "jpg") {
        message = "only jpg images are supported";
        return false;
    }

    const std::string final_path = slot_image_path(request.slot, extension);
    const std::string temp_path  = final_path + ".tmp";
    if (!write_binary_file(temp_path, request.data)) {
        message = "failed to store image";
        return false;
    }

    if (unlink(final_path.c_str()) != 0 && errno != ENOENT) {
        unlink(temp_path.c_str());
        message = "failed to replace existing image";
        return false;
    }

    if (rename(temp_path.c_str(), final_path.c_str()) != 0) {
        unlink(temp_path.c_str());
        message = "failed to finalize image";
        return false;
    }

    for (const auto* candidate : {"png", "jpg", "jpeg"}) {
        if (extension == candidate || (extension == "jpg" && strcmp(candidate, "jpeg") == 0)) {
            continue;
        }
        const std::string old_path = slot_image_path(request.slot, candidate);
        unlink(old_path.c_str());
    }

    if (!write_text_file(slot_meta_path(request.slot), extension)) {
        message = "failed to store image metadata";
        return false;
    }

    // Convert to pre-decoded raw RGB565 cache immediately for instant display
    unlink(slot_raw_path(request.slot).c_str());
    convert_jpg_to_raw(final_path, slot_raw_path(request.slot));

    // Activate the newly uploaded slot
    auto active = read_active_slots();
    if (request.slot < active.size()) {
        active[request.slot] = true;
        persist_active_slots(active);
    }

    mclog::tagInfo(_tag, "badge image saved, slot={}, path={}", request.slot, final_path);
    message = "upload success";
    return true;
}

}  // namespace

bool Hal::loadBadgeImage(lv_obj_t* image)
{
    if (image == nullptr) {
        return false;
    }

    auto active = read_active_slots();
    for (std::size_t s = 0; s < _max_badge_slot_count; ++s) {
        if (active[s] && is_slot_available(s)) {
            return load_badge_slot(image, s);
        }
    }

    const std::size_t fallback_slot = find_first_available_slot();
    if (fallback_slot < _max_badge_slot_count && load_badge_slot(image, fallback_slot)) {
        return true;
    }

    lv_image_set_src(image, &icon_badge);
    mclog::tagWarn(_tag, "badge image not found, use default asset");
    return false;
}

bool Hal::loadNextBadgeImage(lv_obj_t* image)
{
    mclog::tagInfo(_tag, "load next badge image");

    const std::size_t slot = find_next_active_slot(_current_loaded_slot);
    if (slot < _max_badge_slot_count) {
        return load_badge_slot(image, slot);
    }

    lv_image_set_src(image, &icon_badge);
    return false;
}

bool Hal::loadPreviousBadgeImage(lv_obj_t* image)
{
    mclog::tagInfo(_tag, "load previous badge image");

    const std::size_t slot = find_prev_active_slot(_current_loaded_slot);
    if (slot < _max_badge_slot_count) {
        return load_badge_slot(image, slot);
    }

    lv_image_set_src(image, &icon_badge);
    return false;
}

bool Hal::loadNextActiveBadgeImage(lv_obj_t* image)
{
    if (image == nullptr) {
        return false;
    }

    const std::size_t slot = find_next_active_slot(_current_loaded_slot);
    if (slot == _current_loaded_slot) {
        return true;
    }

    if (slot < _max_badge_slot_count) {
        return load_badge_slot(image, slot);
    }

    return false;
}

uint32_t Hal::getBadgeCarouselIntervalMs()
{
    return read_carousel_interval() * 1000;
}

void Hal::setBadgeCarouselIntervalSeconds(uint32_t seconds)
{
    persist_carousel_interval(seconds);
}

bool Hal::isBadgeSlotActive(std::size_t slot)
{
    if (slot >= _max_badge_slot_count) return false;
    return read_active_slots()[slot];
}

bool Hal::setBadgeSlotActive(std::size_t slot, bool active)
{
    std::string msg;
    return set_active_badge_slot(slot, active, msg);
}

bool Hal::startBadgeEditModeViaAp(std::function<void(std::string_view)> onLog)
{
    mclog::tagInfo(_tag, "start badge edit mode via AP");

    if (!ensure_badge_dir()) {
        if (onLog) {
            onLog("Failed to create badge storage directory");
        }
        return false;
    }

    wifi_enter_badge_ap();
    bool exitToHome = false;
    badge::config_ap::run(onLog,
                          {
                              .onUpload      = save_badge_upload,
                              .onGetState    = make_badge_state,
                              .onGetImage    = get_badge_image,
                              .onSetActive   = set_active_badge_slot,
                              .onSetInterval = set_carousel_interval,
                              .onDelete      = delete_badge_slot,
                          },
                          &exitToHome);
    wifi_exit_badge_ap();
    return exitToHome;
}

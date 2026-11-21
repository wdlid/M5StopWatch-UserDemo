/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <string>

namespace weather {

enum class AqiLevel : uint8_t {
    Unknown = 0,
    Good,
    Moderate,
    UnhealthySg,  // Unhealthy for sensitive groups
    Unhealthy,
    VeryUnhealthy,
    Hazardous,
};

struct Snapshot {
    bool valid             = false;
    std::string temperature;  // e.g. "22C"
    std::string condition;    // e.g. "Clear"
    std::string aqiText;      // e.g. "AQ9"
    int aqiValue           = -1;
    AqiLevel aqiLevel      = AqiLevel::Unknown;
};

/**
 * @brief Non-blocking weather cache for the launcher clock arc.
 * Call tick() periodically (e.g. from ArcTopClock::update).
 */
void tick();

Snapshot snapshot();

}  // namespace weather

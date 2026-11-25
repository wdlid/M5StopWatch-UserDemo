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
    bool valid = false;
    bool hasTemp = false;
    int tempC = 0;
    std::string temperature;   // ASCII e.g. "22C" (used by ArcTopClock)
    std::string tempDisplay;   // Display e.g. "22°C"
    std::string condition;     // e.g. "Clear", "Cloudy", "Rain", "Snow"
    std::string windDir;       // e.g. "SW", "NE", "Calm"
    std::string windLevel;     // e.g. "Lv.2", "Lv.3-4"
    std::string windText;      // e.g. "SW Lv.2"
    std::string humidity;      // e.g. "45%"
    std::string aqiText;       // e.g. "AQ30" (used by ArcTopClock)
    int aqiValue      = -1;
    AqiLevel aqiLevel = AqiLevel::Unknown;
};

/**
 * @brief Non-blocking weather cache for the launcher clock arc.
 * Call tick() periodically (e.g. from ArcTopClock::update).
 */
void tick();

Snapshot snapshot();

}  // namespace weather

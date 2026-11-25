#pragma once
#include <cstdint>

using BaseType_t = int;
using TickType_t = uint32_t;
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFFUL
#define pdMS_TO_TICKS(ms) (ms)

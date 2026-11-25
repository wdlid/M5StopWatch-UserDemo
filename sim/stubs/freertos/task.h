#pragma once
#include "FreeRTOS.h"

using TaskHandle_t = void*;
inline void vTaskDelay(TickType_t) {}
inline void vTaskDelete(TaskHandle_t) {}

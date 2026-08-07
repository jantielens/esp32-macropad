#pragma once

#include "FreeRTOS.h"

inline TaskHandle_t test_freertos_current_task = nullptr;
inline bool test_freertos_in_isr = false;

inline TaskHandle_t xTaskGetCurrentTaskHandle() {
    return test_freertos_current_task;
}

inline bool xPortInIsrContext() {
    return test_freertos_in_isr;
}
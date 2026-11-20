#pragma once

#include "FreeRTOS.h"

inline TaskHandle_t test_freertos_current_task = nullptr;
inline bool test_freertos_in_isr = false;
inline uint8_t* test_freertos_stack_start = reinterpret_cast<uint8_t*>(4096);

inline uint8_t* pxTaskGetStackStart(TaskHandle_t) {
    return test_freertos_stack_start;
}

inline TaskHandle_t xTaskGetCurrentTaskHandle() {
    return test_freertos_current_task;
}

inline bool xPortInIsrContext() {
    return test_freertos_in_isr;
}
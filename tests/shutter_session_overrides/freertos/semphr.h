// ============================================================================
// Test override: freertos/semphr.h — single-threaded no-op mutex
// ============================================================================
#pragma once
#include "FreeRTOS.h"

typedef void* SemaphoreHandle_t;

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return (SemaphoreHandle_t)(uintptr_t)1;
}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t)             { return pdTRUE; }

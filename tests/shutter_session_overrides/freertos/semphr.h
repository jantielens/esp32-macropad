// ============================================================================
// Test override: freertos/semphr.h
// ============================================================================
#pragma once
#include "FreeRTOS.h"
#include <mutex>

typedef void* SemaphoreHandle_t;

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return new std::mutex;
}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t) {
    static_cast<std::mutex*>(mutex)->lock();
    return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    static_cast<std::mutex*>(mutex)->unlock();
    return pdTRUE;
}
inline void vSemaphoreDelete(SemaphoreHandle_t mutex) {
    delete static_cast<std::mutex*>(mutex);
}

#pragma once
#include "FreeRTOS.h"
#include <mutex>
#include <new>

using SemaphoreHandle_t = std::mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new (std::nothrow) std::mutex; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t) {
    if (!mutex) return pdFALSE;
    mutex->lock();
    return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    if (!mutex) return pdFALSE;
    mutex->unlock();
    return pdTRUE;
}
inline void vSemaphoreDelete(SemaphoreHandle_t mutex) { delete mutex; }

#pragma once

#include <freertos/FreeRTOS.h>
#include <mutex>
#include <new>

typedef std::mutex* SemaphoreHandle_t;

inline int timer_test_mutex_create_calls = 0;
inline int timer_test_mutex_fail_on_call = 0;

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    timer_test_mutex_create_calls++;
    if (timer_test_mutex_create_calls == timer_test_mutex_fail_on_call) return nullptr;
    return new (std::nothrow) std::mutex();
}
inline int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t) {
    semaphore->lock();
    return 1;
}
inline int xSemaphoreGive(SemaphoreHandle_t semaphore) {
    semaphore->unlock();
    return 1;
}

#define portMAX_DELAY ((TickType_t)-1)

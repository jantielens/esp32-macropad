// ============================================================================
// Host-test stub for freertos/semphr.h
// Provides minimal FreeRTOS semaphore/mutex definitions used by shutter_measure.cpp
// ============================================================================
#pragma once
#include "FreeRTOS.h"

#include <chrono>

typedef StaticSemaphore_t* SemaphoreHandle_t;
typedef void (*TestSemaphoreTakeHook)(SemaphoreHandle_t semaphore, TickType_t ticks);

#ifndef portMAX_DELAY
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#endif

inline TestSemaphoreTakeHook test_freertos_semaphore_take_hook = nullptr;
inline std::atomic<bool> test_freertos_force_wait_timeout = false;
inline std::atomic<uint32_t> test_freertos_successful_takes = 0;

inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage) {
	if (storage) {
		std::lock_guard<std::mutex> lock(storage->mutex);
		storage->signaled = false;
	}
	return storage;
}

inline SemaphoreHandle_t xSemaphoreCreateBinary() {
	return xSemaphoreCreateBinaryStatic(new StaticSemaphore_t{});
}

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
	SemaphoreHandle_t semaphore = xSemaphoreCreateBinary();
	if (semaphore) {
		std::lock_guard<std::mutex> lock(semaphore->mutex);
		semaphore->signaled = true;
	}
	return semaphore;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
	if (!semaphore) return pdFALSE;
	{
		std::lock_guard<std::mutex> lock(semaphore->mutex);
		if (semaphore->signaled) {
			semaphore->signaled = false;
			test_freertos_successful_takes++;
			return pdTRUE;
		}
	}
	if (test_freertos_semaphore_take_hook) {
		test_freertos_semaphore_take_hook(semaphore, ticks);
	}
	if (ticks && ticks != portMAX_DELAY && test_freertos_force_wait_timeout) return pdFALSE;

	std::unique_lock<std::mutex> lock(semaphore->mutex);
	if (ticks == portMAX_DELAY) {
		semaphore->cv.wait(lock, [semaphore] { return semaphore->signaled; });
	} else if (ticks != 0 && !semaphore->signaled) {
		semaphore->cv.wait_for(lock, std::chrono::milliseconds(ticks),
				[semaphore] { return semaphore->signaled; });
	}
	if (!semaphore->signaled) return pdFALSE;
	semaphore->signaled = false;
	test_freertos_successful_takes++;
	return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
	if (!semaphore) return pdFALSE;
	{
		std::lock_guard<std::mutex> lock(semaphore->mutex);
		semaphore->signaled = true;
	}
	semaphore->cv.notify_one();
	return pdTRUE;
}

inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
	if (!semaphore) return;
	std::lock_guard<std::mutex> lock(semaphore->mutex);
	semaphore->signaled = false;
}

// ============================================================================
// Host-test stub for FreeRTOS.h
// Provides the minimal definitions used by shutter_measure.cpp
// ============================================================================
#pragma once
#include <atomic>
#include <condition_variable>
#include <stdint.h>
#include <mutex>

typedef struct {} StaticTask_t;
typedef struct {
	std::mutex mutex;
	std::condition_variable cv;
	bool signaled = false;
} StaticSemaphore_t;
typedef void* TaskHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

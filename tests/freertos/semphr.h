// ============================================================================
// Host-test stub for freertos/semphr.h
// Provides minimal FreeRTOS semaphore/mutex definitions used by shutter_measure.cpp
// ============================================================================
#pragma once
#include "FreeRTOS.h"

typedef void* SemaphoreHandle_t;

// Return a non-null sentinel so null-guard checks in init pass.
#define xSemaphoreCreateMutex()            ((SemaphoreHandle_t)(uintptr_t)1)

// Cast-to-void to suppress -Wunused-value under -Werror; parameter casts
// avoid -Wunused-parameter warnings from the callers.
#define xSemaphoreTake(mutex, ticks)       ((void)(mutex), (void)(ticks))
#define xSemaphoreGive(mutex)              ((void)(mutex))

#ifndef portMAX_DELAY
#define portMAX_DELAY                      ((TickType_t)0xffffffffUL)
#endif

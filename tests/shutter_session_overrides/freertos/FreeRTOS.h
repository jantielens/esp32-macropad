// ============================================================================
// Test override: freertos/FreeRTOS.h
// ============================================================================
#pragma once
#include <stdint.h>

typedef int      BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;
typedef void*    TaskHandle_t;
typedef struct {} StaticTask_t;

#define pdTRUE  ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdPASS  pdTRUE
#define pdFAIL  pdFALSE

#ifndef portMAX_DELAY
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#endif

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

typedef void (*TaskFunction_t)(void*);

// Synchronous task stubs — see freertos/task.h for definitions.
inline BaseType_t xTaskCreate(TaskFunction_t fn, const char* /*name*/,
                              uint32_t /*stack*/, void* arg,
                              UBaseType_t /*prio*/, TaskHandle_t* /*handle*/) {
    if (fn) fn(arg);
    return pdPASS;
}
inline void vTaskDelete(TaskHandle_t) {}

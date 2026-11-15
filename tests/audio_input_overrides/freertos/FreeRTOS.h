#pragma once
#include "../../freertos/FreeRTOS.h"
#include "../../freertos/portmacro.h"

#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define portENTER_CRITICAL_ISR(mux) portENTER_CRITICAL(mux)
#define portEXIT_CRITICAL_ISR(mux) portEXIT_CRITICAL(mux)
#define IRAM_ATTR
typedef uint32_t StackType_t;
typedef unsigned UBaseType_t;
typedef void (*TaskFunction_t)(void*);

// ============================================================================
// Host-test stub for FreeRTOS.h
// Provides the minimal definitions used by shutter_measure.cpp
// ============================================================================
#pragma once
#include <stdint.h>
typedef struct {} StaticTask_t;
typedef void* TaskHandle_t;
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

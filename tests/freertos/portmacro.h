// ============================================================================
// Host-test stub for FreeRTOS portmacro.h
// Provides portMUX_TYPE, portENTER_CRITICAL, and portEXIT_CRITICAL stubs
// ============================================================================
#pragma once
typedef struct {} portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {}
static inline void portENTER_CRITICAL(portMUX_TYPE*) {}
static inline void portEXIT_CRITICAL(portMUX_TYPE*) {}

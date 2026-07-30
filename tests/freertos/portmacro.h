// ============================================================================
// Host-test stub for FreeRTOS portmacro.h
// Provides portMUX_TYPE, portENTER_CRITICAL, and portEXIT_CRITICAL stubs
// ============================================================================
#pragma once
#include <mutex>

typedef struct { std::recursive_mutex mutex; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {}
static inline void portENTER_CRITICAL(portMUX_TYPE* mux) { mux->mutex.lock(); }
static inline void portEXIT_CRITICAL(portMUX_TYPE* mux) { mux->mutex.unlock(); }

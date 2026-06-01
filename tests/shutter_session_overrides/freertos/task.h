// ============================================================================
// Test override: freertos/task.h — synchronous task creation
// ============================================================================
//
// xTaskCreate() runs the function body synchronously on the calling thread so
// that test logic can deterministically observe its side effects (e.g. the
// session persist task firing during shutter_session_stop()).
//
// vTaskDelete() is a no-op — the lambda has already returned by the time
// it would be called.
// ============================================================================
#pragma once
#include "FreeRTOS.h"
// xTaskCreate / vTaskDelete are declared in FreeRTOS.h above.

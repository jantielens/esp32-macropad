#pragma once
#include <stddef.h>

/**
 * Allocates memory for config caches, preferring PSRAM if available.
 *
 * Attempts allocation in this order:
 * 1. PSRAM (via heap_caps_calloc with MALLOC_CAP_SPIRAM) if psramFound()
 * 2. Internal SRAM (via calloc) if PSRAM unavailable or allocation failed
 *
 * @param bytes Size to allocate
 * @param tag   Short identifier for logging (e.g., "mqtt_triggers", "hw_buttons")
 * @return Pointer to zero-initialized memory, or nullptr if both attempts fail
 *
 * Logs placement (PSRAM/SRAM) at INFO level, allocation failure at ERROR level.
 * Caller must check for nullptr and handle graceful degradation.
 */
void* config_psram_alloc(size_t bytes, const char* tag);

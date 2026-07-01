#include "config_psram.h"

#include <Arduino.h>  // for psramFound()
#include <esp_heap_caps.h>

#include "log_manager.h"

#define TAG "ConfigPSRAM"

void* config_psram_alloc(size_t bytes, const char* tag) {
    void* ptr = nullptr;

    // Try PSRAM first if available
    if (psramFound()) {
        ptr = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ptr) {
            LOGI(TAG, "%s config: allocated %u bytes in PSRAM", tag, (unsigned)bytes);
            return ptr;
        }
        LOGW(TAG, "%s config: PSRAM allocation failed, trying internal SRAM", tag);
    }

    // Fallback to internal SRAM
    ptr = calloc(1, bytes);
    if (ptr) {
        LOGI(TAG, "%s config: allocated %u bytes in SRAM (PSRAM %s)",
             tag, (unsigned)bytes, psramFound() ? "exhausted" : "unavailable");
        return ptr;
    }

    LOGE(TAG, "%s config: allocation failed (%u bytes) — feature will be disabled",
         tag, (unsigned)bytes);
    return nullptr;
}

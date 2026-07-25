#include "tls_allocator.h"

#include "log_manager.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

namespace {

void *tls_calloc(size_t count, size_t size) {
    void *memory = heap_caps_calloc(count, size,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
        memory = heap_caps_calloc(count, size,
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return memory;
}

void tls_free(void *memory) {
    heap_caps_free(memory);
}

}  // namespace

void tls_allocator_init() {
    if (!psramFound()) return;
    mbedtls_platform_set_calloc_free(tls_calloc, tls_free);
    LOGI("TLS", "Allocator configured for PSRAM (%u bytes)",
         (unsigned)ESP.getPsramSize());
}
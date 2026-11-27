#include "tls_allocator.h"

#if MBEDTLS_PSRAM_THRESHOLD_BYTES > 0
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <mbedtls/platform.h>
#include <stdint.h>

#if !defined(MBEDTLS_PLATFORM_MEMORY) || defined(MBEDTLS_PLATFORM_CALLOC_MACRO) || \
    defined(MBEDTLS_PLATFORM_FREE_MACRO)
#error "MBEDTLS PSRAM allocation requires runtime platform memory hooks"
#endif

namespace {
constexpr uint32_t kPsramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
constexpr uint32_t kInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

void* tls_calloc(size_t count, size_t size) {
    if (!count || !size) return nullptr;
    if (count > SIZE_MAX / size) {
        esp_rom_printf("[TLSAlloc] ERROR: calloc overflow count=%u size=%u\n",
                       static_cast<unsigned>(count), static_cast<unsigned>(size));
        return nullptr;
    }

    const size_t bytes = count * size;
    const uint32_t caps = bytes >= MBEDTLS_PSRAM_THRESHOLD_BYTES ? kPsramCaps : kInternalCaps;
    void* memory = heap_caps_calloc(count, size, caps);
    if (!memory) {
        // The normal logger can allocate; this callback must not re-enter it.
        esp_rom_printf("[TLSAlloc] ERROR: allocation failed bytes=%u caps=0x%08x\n",
                       static_cast<unsigned>(bytes), static_cast<unsigned>(caps));
    }
    return memory;
}
}

void tls_allocator_init() {
    static bool attempted = false;
    if (attempted) return;
    attempted = true;

    if (!psramFound() ||
        heap_caps_get_largest_free_block(kPsramCaps) < MBEDTLS_PSRAM_THRESHOLD_BYTES) {
        esp_rom_printf("[TLSAlloc] ERROR: PSRAM unavailable or insufficient; SDK allocator retained\n");
        return;
    }
    if (mbedtls_platform_set_calloc_free(tls_calloc, heap_caps_free) != 0) {
        esp_rom_printf("[TLSAlloc] ERROR: allocator installation failed\n");
        return;
    }
    esp_rom_printf("[TLSAlloc] Experiment enabled: allocations >= %u bytes in PSRAM, no internal fallback\n",
                   static_cast<unsigned>(MBEDTLS_PSRAM_THRESHOLD_BYTES));
}
#endif

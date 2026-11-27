#include "tls_allocator.h"
#include <esp_heap_caps.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
bool psram_present = true;
size_t psram_largest = 32768;
bool fail_allocation = false;
unsigned allocations = 0;
unsigned installations = 0;
unsigned diagnostics = 0;
uint32_t allocation_caps = 0;
void* (*installed_calloc)(size_t, size_t) = nullptr;
void (*installed_free)(void*) = nullptr;
}

bool psramFound() { return psram_present; }
size_t heap_caps_get_largest_free_block(uint32_t caps) {
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return psram_largest;
}
void* heap_caps_calloc(size_t count, size_t size, uint32_t caps) {
    ++allocations;
    allocation_caps = caps;
    return fail_allocation ? nullptr : std::calloc(count, size);
}
void heap_caps_free(void* ptr) { std::free(ptr); }
int esp_rom_printf(const char*, ...) {
    ++diagnostics;
    return 0;
}
int mbedtls_platform_set_calloc_free(void* (*calloc_func)(size_t, size_t),
                                     void (*free_func)(void*)) {
    ++installations;
    installed_calloc = calloc_func;
    installed_free = free_func;
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1) {
        if (std::strcmp(argv[1], "missing-psram") == 0) psram_present = false;
        else if (std::strcmp(argv[1], "insufficient-psram") == 0) psram_largest = 4095;
        else return 1;
        tls_allocator_init();
        assert(installations == 0 && diagnostics == 1);
        psram_present = true;
        psram_largest = 32768;
        tls_allocator_init();
        assert(installations == 0); // Never install late after failed startup.
        return 0;
    }

    void* earlier_internal_allocation = std::calloc(1, 64);
    assert(earlier_internal_allocation);
    tls_allocator_init();
    tls_allocator_init();
    assert(installations == 1 && installed_calloc && installed_free);
    installed_free(earlier_internal_allocation);
    installed_free(nullptr);

    for (size_t bytes : {size_t{4095}, size_t{4096}, size_t{16384}}) {
        void* memory = installed_calloc(1, bytes);
        assert(memory);
        assert(allocation_caps == ((bytes < 4096 ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM) |
                                   MALLOC_CAP_8BIT));
        const auto* data = static_cast<const uint8_t*>(memory);
        for (size_t i = 0; i < bytes; ++i) assert(data[i] == 0);
        installed_free(memory);
    }
    void* product = installed_calloc(64, 64);
    assert(product && allocation_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    installed_free(product);

    const unsigned before_invalid = allocations;
    const unsigned before_diagnostics = diagnostics;
    assert(!installed_calloc(0, 16384) && !installed_calloc(16384, 0));
    assert(diagnostics == before_diagnostics);
    assert(!installed_calloc(SIZE_MAX / 2 + 1, 2));
    assert(allocations == before_invalid && diagnostics == before_diagnostics + 1);

    fail_allocation = true;
    for (size_t bytes : {size_t{64}, size_t{16384}}) {
        const unsigned before_failure = allocations;
        const unsigned before_log = diagnostics;
        assert(!installed_calloc(1, bytes));
        assert(allocations == before_failure + 1 && diagnostics == before_log + 1);
        assert(allocation_caps == ((bytes < 4096 ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM) |
                                   MALLOC_CAP_8BIT));
    }
    fail_allocation = false;
    void* recovered = installed_calloc(1, 16384);
    assert(recovered && allocation_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    installed_free(recovered);
}

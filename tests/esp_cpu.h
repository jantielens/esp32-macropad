#pragma once

#include <stdint.h>

inline uintptr_t test_esp_stack_pointer = 4096 + 65536;

inline void* esp_cpu_get_sp() {
    return reinterpret_cast<void*>(test_esp_stack_pointer);
}

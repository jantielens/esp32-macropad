// Test stub: esp_heap_caps.h — forward to standard malloc/free
#pragma once
#include <cstdlib>
#include <cstddef>

#define MALLOC_CAP_SPIRAM   0
#define MALLOC_CAP_8BIT     0

inline void* heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
inline void  heap_caps_free(void* ptr) { free(ptr); }

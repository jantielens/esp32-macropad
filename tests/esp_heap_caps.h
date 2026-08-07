// Test stub: esp_heap_caps.h — forwards to standard malloc/free for host builds.
#pragma once
#include <stdlib.h>
#include <stdint.h>

#define MALLOC_CAP_SPIRAM   (1 << 3)
#define MALLOC_CAP_8BIT     (1 << 0)
#define MALLOC_CAP_DMA      (1 << 1)
#define MALLOC_CAP_INTERNAL (1 << 4)

inline void* heap_caps_malloc(size_t size, uint32_t /*caps*/) { return malloc(size); }
inline void* heap_caps_calloc(size_t count, size_t size, uint32_t /*caps*/) { return calloc(count, size); }
inline void* heap_caps_realloc(void* ptr, size_t size, uint32_t /*caps*/) { return realloc(ptr, size); }
inline void  heap_caps_free(void* ptr) { free(ptr); }
inline bool  psramFound() { return false; }

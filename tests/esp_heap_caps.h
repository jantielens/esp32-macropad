// Test stub: esp_heap_caps.h — forwards to standard malloc/free for host builds.
#pragma once
#include <stdlib.h>
#include <stdint.h>

#define MALLOC_CAP_SPIRAM   (1 << 3)
#define MALLOC_CAP_8BIT     (1 << 0)
#define MALLOC_CAP_DMA      (1 << 1)
#define MALLOC_CAP_INTERNAL (1 << 4)

size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);

#ifdef TEST_HEAP_CAPS_ALLOCATOR
void* heap_caps_malloc(size_t size, uint32_t caps);
void heap_caps_free(void* ptr);
bool psramFound();
size_t heap_caps_get_total_size(uint32_t caps);
#else
inline void* heap_caps_malloc(size_t size, uint32_t /*caps*/) { return malloc(size); }
inline void heap_caps_free(void* ptr) { free(ptr); }
inline bool psramFound() { return false; }
#endif
inline void* heap_caps_calloc(size_t count, size_t size, uint32_t /*caps*/) { return calloc(count, size); }
inline void* heap_caps_realloc(void* ptr, size_t size, uint32_t /*caps*/) { return realloc(ptr, size); }

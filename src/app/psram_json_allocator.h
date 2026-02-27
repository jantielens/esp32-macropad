#ifndef PSRAM_JSON_ALLOCATOR_H
#define PSRAM_JSON_ALLOCATOR_H

#include <Arduino.h>
#include <esp_heap_caps.h>

// ArduinoJson-compatible allocator that prefers PSRAM (when available) and
// falls back to internal heap.
//
// Note: This only affects the JsonDocument memory pool. The document object
// itself stays on the stack (small), while the heavy storage is allocated via
// heap_caps_*.
struct PsramJsonAllocator {
		void* allocate(size_t size) {
				if (size == 0) return nullptr;

				void* p = nullptr;
				if (psramFound()) {
						p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
				}
				if (!p) {
						p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
				}
				return p;
		}

		void deallocate(void* ptr) {
				if (ptr) {
						heap_caps_free(ptr);
				}
		}

		void* reallocate(void* ptr, size_t new_size) {
				if (!ptr) return allocate(new_size);
				if (new_size == 0) {
						deallocate(ptr);
						return nullptr;
				}

				// Let ESP-IDF decide the best place to grow/shrink this allocation.
				// (Keeping the original memory region is typically preferable to a copy.)
				return heap_caps_realloc(ptr, new_size, MALLOC_CAP_8BIT);
		}
};

#endif // PSRAM_JSON_ALLOCATOR_H

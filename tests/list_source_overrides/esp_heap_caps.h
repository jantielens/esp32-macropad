#pragma once

#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_SPIRAM (1 << 3)
#define MALLOC_CAP_8BIT (1 << 0)
#define MALLOC_CAP_INTERNAL (1 << 4)

void* heap_caps_malloc(size_t size, uint32_t caps);
void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps);
void heap_caps_free(void* ptr);
bool psramFound();
size_t heap_caps_get_total_size(uint32_t caps);

/*
 * Custom LVGL v9 memory allocator header (LV_STDLIB_CUSTOM backend).
 *
 * The actual symbols (lv_malloc_core, lv_free_core, lv_realloc_core, etc.)
 * are defined in lvgl_heap.cpp and linked directly — no header inclusion
 * needed by LVGL since v9 discovers them at link time.
 *
 * This header is retained ONLY so existing #include "lvgl_heap.h" sites
 * continue to compile without changes.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* These are the v9 LV_STDLIB_CUSTOM backend entry points.
 * LVGL's lv_mem.c calls them; user code should call lv_malloc / lv_free / lv_realloc instead. */
void* lv_malloc_core(size_t size);
void* lv_realloc_core(void* ptr, size_t new_size);
void  lv_free_core(void* ptr);

#ifdef __cplusplus
}
#endif

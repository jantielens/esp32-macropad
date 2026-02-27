/*
 * Custom LVGL memory allocator for ESP32 (LVGL v9 LV_STDLIB_CUSTOM backend).
 *
 * LVGL v9 public API (lv_malloc / lv_free / lv_realloc) calls through to
 * _core() backend functions.  When LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM,
 * the user must supply these symbols.
 *
 * Allocation strategy: PSRAM first → internal 8-bit RAM fallback.
 */

#include "lvgl_heap.h"

#include <lvgl.h>          // lv_mem_monitor_t, lv_mem_pool_t, lv_result_t, LV_UNUSED
#include <esp_heap_caps.h>
#include <string.h>        // memset

static inline bool psram_available() {
#if SOC_SPIRAM_SUPPORTED
		return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
#else
		return false;
#endif
}

// ---------------------------------------------------------------------------
// Core allocation functions (called by lv_malloc / lv_realloc / lv_free)
// ---------------------------------------------------------------------------

extern "C" void* lv_malloc_core(size_t size) {
		if (size == 0) return nullptr;

		if (psram_available()) {
				void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
				if (p) return p;
		}

		return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

extern "C" void* lv_realloc_core(void* ptr, size_t new_size) {
		if (ptr == nullptr) return lv_malloc_core(new_size);
		if (new_size == 0) {
				lv_free_core(ptr);
				return nullptr;
		}

		if (psram_available()) {
				void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
				if (p) return p;
		}

		return heap_caps_realloc(ptr, new_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

extern "C" void lv_free_core(void* ptr) {
		if (!ptr) return;
		heap_caps_free(ptr);
}

// ---------------------------------------------------------------------------
// Lifecycle / diagnostics (required by LV_STDLIB_CUSTOM contract)
// ---------------------------------------------------------------------------

extern "C" void lv_mem_init(void) {
		// Nothing to initialize — we use the system heap directly.
}

extern "C" void lv_mem_deinit(void) {
		// Nothing to tear down.
}

extern "C" lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes) {
		LV_UNUSED(mem);
		LV_UNUSED(bytes);
		return NULL;   // Memory pools not supported with heap_caps backend.
}

extern "C" void lv_mem_remove_pool(lv_mem_pool_t pool) {
		LV_UNUSED(pool);
}

extern "C" void lv_mem_monitor_core(lv_mem_monitor_t* mon_p) {
		if (!mon_p) return;
		memset(mon_p, 0, sizeof(*mon_p));
		// Optionally populate with ESP heap stats in the future.
}

extern "C" lv_result_t lv_mem_test_core(void) {
		return LV_RESULT_OK;
}

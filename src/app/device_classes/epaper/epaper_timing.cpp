#include "epaper_timing.h"

#if HAS_EPAPER

#include <esp_attr.h>

// RTC-retained. Survives deep sleep; zeroed on cold boot.
RTC_DATA_ATTR EpaperTimingBudget epaper_timing_last = {};
RTC_DATA_ATTR static uint32_t s_epaper_wake_id = 0;

uint32_t epaper_timing_begin_wake() {
		const uint32_t next_wake_id = ++s_epaper_wake_id;
		epaper_timing_last = {};
		epaper_timing_last.wake_id = next_wake_id;
		return next_wake_id;
}

void epaper_timing_reset_draw_steps() {
		epaper_timing_last.resolve_ms = 0;
		epaper_timing_last.fetch_ms = 0;
		epaper_timing_last.draw_ms = 0;
		epaper_timing_last.image_from_cache = 0;
}

void epaper_timing_set_resolve_ms(uint32_t ms) {
		epaper_timing_last.resolve_ms = ms;
}

void epaper_timing_set_fetch(uint32_t ms, bool from_cache) {
		epaper_timing_last.fetch_ms = ms;
		epaper_timing_last.image_from_cache = from_cache ? 1 : 0;
}

void epaper_timing_set_draw_ms(uint32_t ms) {
		epaper_timing_last.draw_ms = ms;
}

#endif // HAS_EPAPER

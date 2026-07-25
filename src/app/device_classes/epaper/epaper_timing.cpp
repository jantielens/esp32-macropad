#include "epaper_timing.h"

#if HAS_EPAPER

#include <esp_attr.h>

// RTC-retained. Survives deep sleep; zeroed on cold boot.
RTC_DATA_ATTR EpaperTimingBudget epaper_timing_last = {};

void epaper_timing_reset_draw_steps() {
		epaper_timing_last.resolve_ms = 0;
		epaper_timing_last.fetch_ms = 0;
		epaper_timing_last.draw_ms = 0;
		epaper_timing_last.image_from_cache = 0;
}

void epaper_timing_reset_ble() {
		epaper_timing_last.ble_init_ms = 0;
		epaper_timing_last.ble_scan_ms = 0;
		epaper_timing_last.ble_match_ms = 0;
		epaper_timing_last.ble_ack_tx_ms = 0;
		epaper_timing_last.ble_path = EpaperBlePath::WifiOnly;
		epaper_timing_last.ble_packets_seen = 0;
		epaper_timing_last.ble_rssi = 0;
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

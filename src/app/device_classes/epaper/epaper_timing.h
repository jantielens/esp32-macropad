#pragma once
#ifndef EPAPER_TIMING_H
#define EPAPER_TIMING_H

#include "board_config.h"

#if HAS_EPAPER

#include <stdint.h>

enum class EpaperBlePath : uint8_t {
	WifiOnly,
	BleHit,
	BleMissFallback,
};

// Per-wake timing + diagnostic snapshot. Populated by the e-paper duty cycle,
// retained across deep sleep in RTC memory so the portal can show "last cycle"
// numbers and MQTT can publish them on the next wake.
//
// Cleared on cold boot (power loss / USB unplug) — portal shows zero values
// until the first full cycle completes.
struct EpaperTimingBudget {
		uint32_t boot_to_wifi_ms;   // power-on -> WiFi connected
		int16_t  wifi_rssi;         // captured immediately after WiFi connect
		uint8_t  crc_retry_count;   // attempts made by sidecar fetcher (1 = no retries)
		uint32_t ntp_sync_ms;       // time spent waiting for the per-wake NTP resync
		uint32_t crc_to_draw_ms;    // WiFi connected -> drawImage + display done (includes sidecar fetch)
		uint32_t draw_to_mqtt_ms;   // display done -> MQTT publish done
		uint32_t total_active_ms;   // power-on -> just before deep sleep

		// Image-fetch/render breakdown (a subset of crc_to_draw_ms), staged by the
		// active board driver as each phase of epaper_driver_draw_url()/display()
		// completes. Zero on boards or paths that don't decompose the draw (e.g. a
		// CRC-skip wake, where no fetch/draw happens).
		uint32_t resolve_ms;        // URL/API redirect round-trip (0 if no resolve)
		uint32_t fetch_ms;          // image bytes: SD cache read OR HTTP download
		uint32_t draw_ms;           // framebuffer upload + panel GC16 refresh
		uint8_t  image_from_cache;  // 1 = served from SD cache, 0 = downloaded

		uint32_t ble_init_ms;
		uint32_t ble_scan_ms;
		uint32_t ble_match_ms;
		uint32_t ble_ack_tx_ms;
		EpaperBlePath ble_path;
		uint16_t ble_packets_seen;
		int16_t ble_rssi;
};

extern EpaperTimingBudget epaper_timing_last;

// Sub-step timing setters called by the active e-paper driver as each phase
// completes. They write directly into epaper_timing_last. The duty cycle calls
// epaper_timing_reset_draw_steps() before each refresh so a CRC-skip wake
// reports zeros instead of the previous cycle's values.
void epaper_timing_reset_draw_steps();
void epaper_timing_reset_ble();
void epaper_timing_set_resolve_ms(uint32_t ms);
void epaper_timing_set_fetch(uint32_t ms, bool from_cache);
void epaper_timing_set_draw_ms(uint32_t ms);

#endif // HAS_EPAPER

#endif // EPAPER_TIMING_H

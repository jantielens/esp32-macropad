#pragma once

#include "board_config.h"

#if HAS_EPAPER

#include <stdint.h>

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
		uint32_t crc_to_draw_ms;    // WiFi connected -> drawImage + display done (includes sidecar fetch)
		uint32_t draw_to_mqtt_ms;   // display done -> MQTT publish done
		uint32_t total_active_ms;   // power-on -> just before deep sleep
};

extern EpaperTimingBudget epaper_timing_last;

#endif // HAS_EPAPER

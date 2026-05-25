#pragma once

#include "board_config.h"

#if HAS_EPAPER

#include <stdint.h>

#include "config_manager.h"

// Result of a refresh attempt. Used by the portal "manual refresh" endpoint.
enum class EpaperRefreshResult : uint8_t {
		Skipped = 0,     // Sidecar CRC matched stored value; no redraw performed.
		Updated = 1,     // Image was drawn and pushed to the panel.
		FailedFetch = 2, // Sidecar transport failure AND subsequent draw also failed.
		FailedDraw = 3,  // Draw / display step failed (panel or image decode).
		Disabled = 4,    // HAS_EPAPER is false or URL is empty.
};

struct EpaperRefreshOutcome {
		EpaperRefreshResult result;
		uint32_t crc_used;       // CRC reported by sidecar (0 if unknown)
		int16_t sidecar_http_status; // Last sidecar HTTP status (0 = transport/begin failure)
		uint16_t battery_mv;     // Last battery read (0 if unsupported)
		uint32_t elapsed_ms;     // Total wall-clock for the refresh call
};

// Performs the full wake -> fetch -> draw -> sleep pipeline.
//
// `force` skips the CRC compare so the panel always redraws (used by the
// portal Refresh button so the user can immediately see config changes).
//
// WiFi must already be connected. The panel is initialized on demand and
// always powered down (einkOff) before this returns.
//
// The watchdog is disabled around the long drawImage() call.
EpaperRefreshOutcome epaper_refresh_run(DeviceConfig* config, bool force);

// Unix epoch seconds of the last successful refresh. Persisted in RTC_DATA
// memory so the value survives deep sleep across duty-cycle wakes (but is
// lost on power loss / hard reset). Returns 0 if no refresh has been
// recorded yet, or if the device clock was unsynced at the time of refresh.
uint32_t epaper_refresh_last_unix();

// Last outcome (battery_mv etc.) from the most recent refresh call in this
// boot. Result == Disabled means no refresh has been attempted yet.
EpaperRefreshOutcome epaper_refresh_last_outcome();

// RTC-retained count of successful (Updated) refreshes since cold boot.
uint32_t epaper_refresh_get_count();

#endif // HAS_EPAPER

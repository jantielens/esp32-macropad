#ifndef EPAPER_CONFIG_H
#define EPAPER_CONFIG_H

#include "board_config.h"

#if HAS_EPAPER

#include <stdint.h>

#include "epaper_source_mode.h"

// E-Paper image URL (full HTTP/HTTPS). Sized to fit a realistic dashboard URL.
#ifndef CONFIG_EPAPER_URL_MAX_LEN
#define CONFIG_EPAPER_URL_MAX_LEN 256
#endif

static constexpr uint32_t EPAPER_REFRESH_INTERVAL_DEFAULT_SECONDS = 900;
static constexpr uint32_t EPAPER_REFRESH_INTERVAL_MAX_SECONDS = 86400;

// Carousel entry: URL, per-entry refresh interval, and stay flag (pause rotation when CRC unchanged).
struct EpaperCarouselEntry {
		char url[CONFIG_EPAPER_URL_MAX_LEN];          // empty if slot unused
		uint32_t interval_seconds;                    // required per-slot duration in seconds
		bool stay;                                    // true = don't advance to next entry after refresh
};

// All e-paper-specific configuration lives here, separate from the generic
// DeviceConfig, so the core firmware does not need to know about e-paper.
// The DeviceClass registry owns lifecycle (defaults / load / save / API).
struct EpaperConfig {
		char epaper_url[CONFIG_EPAPER_URL_MAX_LEN];   // resolved runtime URL for current carousel slot (not user-configured)
		uint8_t epaper_rotation;                      // 0..3, default 0
		uint32_t epaper_last_crc32;                   // CRC32 of last successfully rendered image (0 = none)
		bool epaper_crc32_enabled;                    // fetch "<url>.crc32" sidecar to skip unchanged refreshes (default false)
		bool epaper_sd_cache_enabled;                 // cache downloaded image blobs (G16Z/G16P) on microSD to skip the HTTP download on a cache hit (default false; only effective on boards with EPAPER_SD_CS_PIN)
		EpaperImageSourceMode image_source_mode;      // explicit slot-images or display-assignments selection
		char assignment_source_url[CONFIG_EPAPER_URL_MAX_LEN]; // full credential-bearing assignment source URL
		uint32_t assignment_refresh_interval_seconds; // assignment wake cadence, independent of slot durations
		bool ble_assignment_enabled;                  // opt-in BLE assignment acceleration (default false)

		// On-image status overlay
		bool epaper_overlay_enabled;                  // default false
		uint8_t epaper_overlay_position;              // 0=TL, 1=TR, 2=BL, 3=BR (default 3)
		uint8_t epaper_overlay_color;                 // 0=black, 1=darkgray, 2=lightgray, 3=white (default 0)
		uint8_t epaper_overlay_items;                 // bitmask: 0x1=batt icon, 0x2=batt %, 0x4=time, 0x8=cycle ms
		// Frontlight (boards with HAS_EPAPER_FRONTLIGHT only — values are still
		// stored on other boards so the same NVS layout works across upgrades,
		// but the duty cycle ignores them).
		uint8_t epaper_frontlight_brightness;         // 0..63 (0 = disabled, default 0)
		uint16_t epaper_frontlight_duration_s;        // seconds after button wake (default 30)

		// Carousel: 5-slot rotation with per-entry intervals and stay flags.
		uint8_t carousel_count;                       // number of active carousel entries (0..5)
		EpaperCarouselEntry carousel[5];              // 5-slot carousel (empty entries have url[0] == '\0')

		// Schedule: 24-bit hourly mask + UTC offset for battery-aware sleep.
		// schedule_hours: bit N set = hour N is enabled for refresh (0=midnight, 23=11pm).
		// 0x00FFFFFF (default) = all hours enabled (no schedule active).
		// schedule_tz_offset: UTC offset in hours (-12 to +14) for local time calculation.
		uint32_t schedule_hours;                      // 24-bit mask of enabled hours (default 0x00FFFFFF = all)
		int8_t schedule_tz_offset;                    // UTC offset in hours (default 0)
};

// Single global instance, owned by epaper_device_class.cpp. Other e-paper
// modules read directly from this.
extern EpaperConfig g_epaper_config;

// Resolve g_epaper_config.epaper_url from the current carousel slot.
// Returns false when no valid slot URL is available.
bool epaper_resolve_current_url();

// Persist just the rendered-image CRC to NVS without rewriting the entire
// config. Called from the refresh pipeline after a successful redraw so the
// next wake can skip when the sidecar reports the same CRC. The save hook
// already writes all fields; this helper exists only to avoid wearing NVS
// with a full save on every refresh.
void epaper_config_persist_crc(uint32_t crc);

#if HAS_EPAPER_WAKE_BUTTON
enum class EpaperButtonWakeAction : uint8_t {
		None = 0,
		Refresh = 1,
		Config = 2,
};

// True if the most recent wake was triggered by the e-paper wake button (ext1).
bool epaper_button_is_button_wake();
// Boot-time classification of the wake-button press (short = Refresh, long = Config).
EpaperButtonWakeAction epaper_button_wake_action();
#endif

#endif // HAS_EPAPER

#endif // EPAPER_CONFIG_H

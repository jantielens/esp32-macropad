#ifndef EPAPER_CONFIG_H
#define EPAPER_CONFIG_H

#include "board_config.h"

#if HAS_EPAPER

#include <stdint.h>

// E-Paper image URL (full HTTP/HTTPS). Sized to fit a realistic dashboard URL.
#ifndef CONFIG_EPAPER_URL_MAX_LEN
#define CONFIG_EPAPER_URL_MAX_LEN 256
#endif

// All e-paper-specific configuration lives here, separate from the generic
// DeviceConfig, so the core firmware does not need to know about e-paper.
// The DeviceClass registry owns lifecycle (defaults / load / save / API).
struct EpaperConfig {
		char epaper_url[CONFIG_EPAPER_URL_MAX_LEN];   // full HTTP(S) URL of the dashboard image
		uint8_t epaper_rotation;                      // 0..3, default 0
		uint32_t epaper_last_crc32;                   // CRC32 of last successfully rendered image (0 = none)

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
};

// Single global instance, owned by epaper_device_class.cpp. Other e-paper
// modules read directly from this.
extern EpaperConfig g_epaper_config;

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

// True if the most recent wake was triggered by the e-paper wake button (ext0).
bool epaper_button_is_button_wake();
// Boot-time classification of the wake-button press (short = Refresh, long = Config).
EpaperButtonWakeAction epaper_button_wake_action();
#endif

#endif // HAS_EPAPER

#endif // EPAPER_CONFIG_H

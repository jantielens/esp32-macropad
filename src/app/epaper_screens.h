#pragma once

#include <stdint.h>
#include "board_config.h"

#if HAS_EPAPER

// ----------------------------------------------------------------------------
// E-paper full-screen status screens
// ----------------------------------------------------------------------------
// Each screen draws to the panel framebuffer using the e-paper HAL but does
// NOT call epaper_driver_display() — the caller decides when to push the
// frame and whether to power-cycle the panel afterward. This lets duty_cycle
// composite a status screen and an overlay in the same paint pass when
// useful, and lets callers measure timing without the panel waveform inside
// their window.
//
// Callers are also responsible for epaper_driver_begin() / epaper_driver_sleep().
// ----------------------------------------------------------------------------

// Cold-start splash: device name + firmware version + helpful first-boot hint.
// Shown on cold boot before WiFi connect, and on any button wake before
// network operations. Intended to make the device feel alive while the slow
// network path runs.
void epaper_screen_boot_splash(const char* device_name, const char* version);

// Config / AP mode screen: SSID + IP + instructions. is_ap=true means we are
// hosting an AP (show "Connect to <ssid>"), false means STA recovery portal
// (show "Browse to http://<ip>/").
void epaper_screen_config_mode(const char* ssid, const char* ip, bool is_ap);

// Error screen: shown when a refresh fails. retry_seconds=0 means no auto-
// retry is scheduled (e.g. button-only mode).
void epaper_screen_error(const char* error_detail, uint32_t retry_seconds);

// Low-battery screen: shown when battery is below the low-battery threshold.
// The duty cycle uses this immediately on wake and skips the rest of the
// refresh so a depleted cell does not get drained further by WiFi + panel.
void epaper_screen_low_battery(uint16_t mv, uint8_t pct);

#endif // HAS_EPAPER

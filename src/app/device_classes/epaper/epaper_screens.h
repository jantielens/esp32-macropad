#pragma once

#include <stdint.h>
#include "board_config.h"
#include "epaper_driver.h"

#if HAS_EPAPER

// ----------------------------------------------------------------------------
// E-paper status-screen helper
// ----------------------------------------------------------------------------
// Wraps the recurring 5-call dance (begin → set_rotation → paint → display →
// sleep) used by every full-panel status screen draw. The paint callable
// invokes one of the epaper_screen_* functions below (or composites several).
// Templated so callers can pass a lambda that captures local state without
// std::function overhead.
template <typename Fn>
inline void epaper_show_status(uint8_t rotation, Fn paint) {
	if (epaper_driver_begin()) {
		epaper_driver_set_rotation(rotation);
		paint();
		epaper_driver_display();
		epaper_driver_sleep();
	}
}

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
// Shown on cold boot only — button and timer wakes skip the splash to avoid
// the second full-panel refresh on slow 3-bit panels (~10 s on Inkplate 5V2).
void epaper_screen_boot_splash(const char* device_name, const char* version);

// "Refreshing now" feedback shown on button wake when the board opts in via
// EPAPER_FAST_REFRESH. Skipped on slow panels where the extra full refresh
// costs more than the feedback gain.
void epaper_screen_manual_refresh(const char* message);

// Immediate ack drawn BEFORE WiFi/AP setup when the device is entering Config
// or AP mode. Lets the user see that their long-press / reset-burst was
// registered without waiting for the full SSID/IP screen that needs WiFi to
// be up first. The full epaper_screen_config_mode() screen is still drawn
// afterwards once SSID and IP are known.
void epaper_screen_config_mode_starting();

// Config / AP mode screen: SSID + IP + instructions. is_ap=true means we are
// hosting an AP (show "Connect to <ssid>"), false means STA recovery portal
// (show "Browse to http://<ip>/").
void epaper_screen_config_mode(const char* ssid, const char* ip, bool is_ap);

// Brief feedback drawn when the wake button is pressed in Config / AP mode
// to acknowledge the exit request before ESP.restart() takes the device back
// into its configured operating mode.
void epaper_screen_returning_to_normal();

// Error screen: shown when a refresh fails. retry_seconds=0 means no auto-
// retry is scheduled (e.g. button-only mode).
void epaper_screen_error(const char* error_detail, uint32_t retry_seconds);

// Low-battery screen: shown when battery is below the low-battery threshold.
// The duty cycle uses this immediately on wake and skips the rest of the
// refresh so a depleted cell does not get drained further by WiFi + panel.
void epaper_screen_low_battery(uint16_t mv, uint8_t pct);

#endif // HAS_EPAPER

#include "board_config.h"

#if HAS_DISPLAY

#include "screen_saver_manager.h"
#include "log_manager.h"
#include "display_manager.h"

#if HAS_TOUCH
#include "touch_manager.h"
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

namespace {

DeviceConfig* g_config = nullptr;
ScreenSaverState g_state = ScreenSaverState::Awake;
bool g_prev_enabled = false;

uint32_t g_last_activity_ms = 0;
uint32_t g_fade_start_ms = 0;
uint32_t g_fade_duration_ms = 0;
uint8_t g_fade_from = 0;
uint8_t g_fade_to = 0;

uint8_t g_current_brightness = 100;
uint8_t g_target_brightness = 100;

// Cross-task signaling (API handlers run on AsyncTCP task)
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool g_pending_wake = false;
volatile bool g_pending_sleep = false;
volatile bool g_pending_activity = false;
volatile bool g_pending_activity_wake = false;

#if HAS_TOUCH
bool g_prev_touch = false;
#endif

static bool is_enabled() {
		if (!g_config) return false;
		return g_config->screen_saver_enabled;
}

static uint32_t timeout_ms() {
		if (!g_config) return 0;
		return (uint32_t)g_config->screen_saver_timeout_seconds * 1000UL;
}

static uint16_t fade_out_ms() {
		if (!g_config) return 0;
		return g_config->screen_saver_fade_out_ms;
}

static uint16_t fade_in_ms() {
		if (!g_config) return 0;
		return g_config->screen_saver_fade_in_ms;
}

static uint8_t config_brightness() {
		if (!g_config) return 100;
		uint8_t b = g_config->backlight_brightness;
		if (b > 100) b = 100;
		return b;
}

static void apply_brightness(uint8_t brightness) {
		if (!displayManager || !displayManager->getDriver()) return;

		DisplayDriver* driver = displayManager->getDriver();

		if (driver->hasBacklightControl()) {
				driver->setBacklightBrightness(brightness);
		} else {
				// Fallback: on/off only
				driver->setBacklight(brightness > 0);
		}
}

static void start_fade(ScreenSaverState newState, uint8_t from, uint8_t to, uint16_t duration_ms) {
		g_state = newState;
		g_fade_start_ms = millis();
		g_fade_duration_ms = duration_ms;
		g_fade_from = from;
		g_fade_to = to;
		g_target_brightness = to;

		// If duration is 0, apply immediately.
		if (duration_ms == 0) {
				g_current_brightness = to;
				apply_brightness(to);
				g_state = (to == 0) ? ScreenSaverState::Asleep : ScreenSaverState::Awake;
				return;
		}

		// Apply the starting brightness right away to avoid a one-loop delay.
		g_current_brightness = from;
		apply_brightness(from);
}

static void request_activity(bool wake) {
		portENTER_CRITICAL(&g_mux);
		g_pending_activity = true;
		g_pending_activity_wake = wake;
		portEXIT_CRITICAL(&g_mux);
}

static void request_wake() {
		portENTER_CRITICAL(&g_mux);
		g_pending_wake = true;
		portEXIT_CRITICAL(&g_mux);
}

static void request_sleep() {
		portENTER_CRITICAL(&g_mux);
		g_pending_sleep = true;
		portEXIT_CRITICAL(&g_mux);
}

static void handle_pending_requests() {
		bool doWake = false;
		bool doSleep = false;
		bool doActivity = false;
		bool activityWake = false;

		portENTER_CRITICAL(&g_mux);
		doWake = g_pending_wake;
		doSleep = g_pending_sleep;
		doActivity = g_pending_activity;
		activityWake = g_pending_activity_wake;
		g_pending_wake = false;
		g_pending_sleep = false;
		g_pending_activity = false;
		g_pending_activity_wake = false;
		portEXIT_CRITICAL(&g_mux);

		if (doActivity) {
				g_last_activity_ms = millis();
				// Only escalate "activity" into a wake when we're actually asleep/dimming.
				// When awake, touches should flow to LVGL without causing redundant wakes.
				if (activityWake && (g_state == ScreenSaverState::Asleep || g_state == ScreenSaverState::FadingOut)) {
						doWake = true;
				}
		}

		if (doSleep) {
				// If both are requested, prefer wake.
				// Sleep is a manual override and should work even when the feature is disabled.
				if (!doWake) {
						const uint8_t from = g_current_brightness;
						start_fade(ScreenSaverState::FadingOut, from, 0, fade_out_ms());
						LOGI("SAVER", "Sleep requested");
				}
		}

		if (doWake) {
				g_last_activity_ms = millis();
				const uint8_t target = config_brightness();
				const uint8_t from = g_current_brightness;

				// Already awake at target brightness; nothing to do.
				if (g_state == ScreenSaverState::Awake && from == target) {
						return;
				}

				#if HAS_TOUCH
				// Swallow wake interactions so swipe-to-wake doesn't click through into LVGL.
				// We suppress for (fade_in + small buffer) and only when waking from sleep/dimming.
				if (g_state == ScreenSaverState::Asleep || g_state == ScreenSaverState::FadingOut) {
						const uint32_t windowMs = (uint32_t)fade_in_ms() + 250;
						touch_manager_suppress_lvgl_input(windowMs);
				}
				#endif

				start_fade(ScreenSaverState::FadingIn, from, target, fade_in_ms());
				LOGI("SAVER", "Wake requested");
		}
}

static void update_fade() {
		if (g_state != ScreenSaverState::FadingOut && g_state != ScreenSaverState::FadingIn) {
				return;
		}

		if (g_fade_duration_ms == 0) {
				return;
		}

		const uint32_t now = millis();
		const uint32_t elapsed = now - g_fade_start_ms;

		if (elapsed >= g_fade_duration_ms) {
				g_current_brightness = g_fade_to;
				apply_brightness(g_fade_to);
				g_state = (g_fade_to == 0) ? ScreenSaverState::Asleep : ScreenSaverState::Awake;
				return;
		}

		// Linear interpolation: from + (to-from) * t
		const float t = (float)elapsed / (float)g_fade_duration_ms;
		const int delta = (int)g_fade_to - (int)g_fade_from;
		int value = (int)g_fade_from + (int)((float)delta * t);
		if (value < 0) value = 0;
		if (value > 100) value = 100;

		const uint8_t newBrightness = (uint8_t)value;
		if (newBrightness != g_current_brightness) {
				g_current_brightness = newBrightness;
				apply_brightness(newBrightness);
		}
}

static void maybe_auto_sleep() {
		if (!is_enabled()) return;

		// Only auto-sleep from awake.
		if (g_state != ScreenSaverState::Awake) return;

		const uint32_t toMs = timeout_ms();
		if (toMs == 0) return;

		const uint32_t now = millis();
		if (now - g_last_activity_ms >= toMs) {
				start_fade(ScreenSaverState::FadingOut, g_current_brightness, 0, fade_out_ms());
				LOGI("SAVER", "Auto-sleep (timeout)");
		}
}

#if HAS_TOUCH
static void poll_touch_activity() {
		if (!g_config) return;
		if (!g_config->screen_saver_wake_on_touch) return;

		// Avoid competing with LVGL's indev polling while awake.
		// Only poll the raw touch state to wake the backlight when sleeping/dimming.
		if (g_state == ScreenSaverState::Awake || g_state == ScreenSaverState::FadingIn) return;

		const bool touched = touch_manager_is_touched();
		const bool pressedEdge = touched && !g_prev_touch;
		g_prev_touch = touched;

		if (pressedEdge) {
				// Touch press = activity + wake.
				request_activity(true);
		}
}
#endif

} // namespace

void screen_saver_manager_init(DeviceConfig* config) {
		g_config = config;
		g_state = ScreenSaverState::Awake;
		g_last_activity_ms = millis();
		g_prev_enabled = is_enabled();

		// Clear any early-boot cross-task requests for deterministic startup.
		portENTER_CRITICAL(&g_mux);
		g_pending_wake = false;
		g_pending_sleep = false;
		g_pending_activity = false;
		g_pending_activity_wake = false;
		portEXIT_CRITICAL(&g_mux);

		#if HAS_TOUCH
		g_prev_touch = false;
		#endif

		// Initialize brightness tracking from config.
		g_target_brightness = config_brightness();
		g_current_brightness = g_target_brightness;

		// Best-effort: sync with driver’s current brightness if available.
		if (displayManager && displayManager->getDriver() && displayManager->getDriver()->hasBacklightControl()) {
				uint8_t driverBrightness = displayManager->getDriver()->getBacklightBrightness();
				if (driverBrightness > 100) driverBrightness = 100;
				g_current_brightness = driverBrightness;
		}

		LOGI("SAVER", "Init: enabled=%d timeout=%us fade_out=%ums fade_in=%ums wake_touch=%d",
				(int)(g_config ? g_config->screen_saver_enabled : 0),
				(unsigned)(g_config ? g_config->screen_saver_timeout_seconds : 0),
				(unsigned)fade_out_ms(),
				(unsigned)fade_in_ms(),
				(int)(g_config ? g_config->screen_saver_wake_on_touch : 0)
		);
}

void screen_saver_manager_loop() {
		if (!g_config) return;

		// Touch polling is in the main loop task; safe.
		#if HAS_TOUCH
		poll_touch_activity();
		#endif

		// If the feature was just disabled, immediately wake the display.
		const bool enabledNow = is_enabled();
		if (g_prev_enabled && !enabledNow) {
				request_wake();
		}
		g_prev_enabled = enabledNow;

		handle_pending_requests();

		update_fade();
		maybe_auto_sleep();

		#if HAS_TOUCH
		// While dimming/asleep/fading in, suppress LVGL input so wake gestures don't click-through.
		// This is based on state (not config enabled), so it also protects transitions caused
		// by explicit API calls.
		static bool prev_force = false;
		const bool force = (g_state != ScreenSaverState::Awake);
		if (force != prev_force) {
				touch_manager_set_lvgl_force_released(force);
				LOGI("SAVER", "Touch suppress %s", force ? "ON" : "OFF");
				prev_force = force;
		}
		#endif
}

void screen_saver_manager_notify_activity(bool wake) {
		request_activity(wake);
}

void screen_saver_manager_sleep_now() {
		request_sleep();
}

void screen_saver_manager_wake() {
		request_wake();
}

bool screen_saver_manager_is_asleep() {
		return g_state == ScreenSaverState::Asleep || g_state == ScreenSaverState::FadingOut;
}

ScreenSaverStatus screen_saver_manager_get_status() {
		ScreenSaverStatus status;
		status.enabled = is_enabled();
		status.state = g_state;
		status.current_brightness = g_current_brightness;
		status.target_brightness = g_target_brightness;

		status.seconds_until_sleep = 0;
		if (status.enabled && g_state == ScreenSaverState::Awake) {
				const uint32_t toMs = timeout_ms();
				const uint32_t now = millis();
				if (toMs > 0 && now >= g_last_activity_ms) {
						const uint32_t elapsed = now - g_last_activity_ms;
						if (elapsed < toMs) {
								status.seconds_until_sleep = (toMs - elapsed + 999) / 1000;
						}
				}
		}

		return status;
}

#endif // HAS_DISPLAY

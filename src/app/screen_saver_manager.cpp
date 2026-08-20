#include "board_config.h"

#if HAS_DISPLAY

#include "screen_saver_manager.h"
#include "button_defaults.h"
#include "screen_saver_schedule.h"
#include "log_manager.h"
#include "display_manager.h"
#if HAS_IMAGE_FETCH
#include "image_fetch.h"
#endif

#if HAS_TOUCH
#include "touch_manager.h"
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/task.h>
#include <atomic>

namespace {

DeviceConfig* g_config = nullptr;
std::atomic<ScreenSaverState> g_state{ScreenSaverState::Awake};
bool g_idle_screen_active = false;
bool g_idle_screen_attempted = false;

// Sleep overlay: opaque black layer on lv_layer_top() while asleep.
// Prevents stale content from showing through on displays without true backlight off.
static lv_obj_t* g_overlay = nullptr;

static void create_sleep_overlay() {
		if (g_overlay) return;
		if (!displayManager) return;
		displayManager->lock();
		g_overlay = lv_obj_create(lv_layer_top());
		lv_obj_remove_style_all(g_overlay);
		lv_obj_set_size(g_overlay, LV_PCT(100), LV_PCT(100));
		lv_obj_set_style_bg_color(g_overlay, lv_color_black(), 0);
		lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
		displayManager->unlock();
}

static void remove_sleep_overlay() {
		if (!g_overlay) return;
		if (!displayManager) return;
		displayManager->lock();
		lv_obj_del(g_overlay);
		g_overlay = nullptr;
		displayManager->unlock();
}

// Pixel shift: offset that advances each sleep cycle. Its configurable range
// is shared with pad layout's reserved margin.
static uint16_t g_pixel_shift_counter = 0;

// Timestamp of last periodic sleep-refresh (for displayRefreshSleep ticking).
static uint32_t g_last_sleep_refresh_ms = 0;

// Centralised state-entry helpers so sleep/wake side-effects live in one place.
static void enter_asleep() {
		const uint8_t distance = button_defaults_get_pixel_shift_distance();
		const uint16_t side = 2 * distance + 1;
		g_pixel_shift_counter = distance ? (g_pixel_shift_counter + 1) % (side * side) : 0;
		create_sleep_overlay();

		// Send panel sleep commands (Display Off + Sleep In) where supported.
		// Lock serializes with the LVGL flush path on the display bus.
		if (displayManager && displayManager->getDriver()) {
				displayManager->lock();
				displayManager->getDriver()->displaySleep();
				displayManager->unlock();
		}

		// Preserve the captured Idle Screen resume target. Direct Display Sleep
		// retains the existing per-screen wake redirect behavior.
		if (displayManager && !g_idle_screen_active) {
				displayManager->handleSleepScreenRedirect();
		}

		// Set state last so the LVGL task doesn't throttle until all
		// transition work (overlay, panel sleep, redirect) is complete.
		g_state = ScreenSaverState::Asleep;
		g_last_sleep_refresh_ms = millis();
}

static void enter_awake() {
		g_state = ScreenSaverState::Awake;
		remove_sleep_overlay();
}
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
volatile bool g_pending_brightness = false;
volatile uint8_t g_pending_brightness_value = 0;

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

static bool idle_screen_enabled() {
		return g_config && g_config->idle_screen_enabled && g_config->idle_screen_pad[0] != '\0';
}

static uint32_t idle_screen_timeout_ms() {
		if (!g_config) return 0;
		return (uint32_t)g_config->idle_screen_timeout_seconds * 1000UL;
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

static void complete_fade_out();

static void start_fade(ScreenSaverState newState, uint8_t from, uint8_t to, uint16_t duration_ms) {
		g_state = newState;
		g_fade_start_ms = millis();
		g_fade_duration_ms = duration_ms;
		g_fade_from = from;
		g_fade_to = to;
		g_target_brightness = to;

#if HAS_IMAGE_FETCH
		// Suspend/unsuspend image fetching on sleep/wake transitions.
		// Uses the global gate so per-slot pause state (page visibility) is preserved.
		if (newState == ScreenSaverState::FadingOut) {
				image_fetch_suspend();
		} else if (newState == ScreenSaverState::FadingIn) {
				image_fetch_unsuspend();
		}
#endif

		// If duration is 0, apply immediately.
		if (duration_ms == 0) {
				g_current_brightness = to;
				apply_brightness(to);
				if (to == 0) complete_fade_out();
				else enter_awake();
				return;
		}

		// Apply the starting brightness right away to avoid a one-loop delay.
		g_current_brightness = from;
		apply_brightness(from);
}

static void complete_fade_out() {
		enter_asleep();
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

static void request_brightness(uint8_t brightness) {
		portENTER_CRITICAL(&g_mux);
		g_pending_brightness = true;
		g_pending_brightness_value = brightness;
		portEXIT_CRITICAL(&g_mux);
}

static void handle_pending_requests() {
		bool doWake = false;
		bool doSleep = false;
		bool doActivity = false;
		bool activityWake = false;
		bool doBrightness = false;
		uint8_t brightnessValue = 0;

		portENTER_CRITICAL(&g_mux);
		doWake = g_pending_wake;
		doSleep = g_pending_sleep;
		doActivity = g_pending_activity;
		activityWake = g_pending_activity_wake;
		doBrightness = g_pending_brightness;
		brightnessValue = g_pending_brightness_value;
		g_pending_wake = false;
		g_pending_sleep = false;
		g_pending_activity = false;
		g_pending_activity_wake = false;
		g_pending_brightness = false;
		portEXIT_CRITICAL(&g_mux);

		if (doActivity) {
				g_last_activity_ms = millis();
				// Only escalate "activity" into a wake when we're actually asleep/dimming.
				// When awake, touches should flow to LVGL without causing redundant wakes.
				if (activityWake && (g_state == ScreenSaverState::Asleep || g_state == ScreenSaverState::FadingOut || g_idle_screen_active)) {
						doWake = true;
				}
		}

		if (doBrightness) {
				// Update the in-RAM config so wake target stays consistent.
				if (g_config) g_config->backlight_brightness = brightnessValue;

				if (g_state == ScreenSaverState::Awake) {
						g_current_brightness = brightnessValue;
						g_target_brightness = brightnessValue;
						apply_brightness(brightnessValue);
						g_last_activity_ms = millis();
				} else {
						// Config updated; wake will fade to the new target.
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
				g_idle_screen_attempted = false;
				if (g_idle_screen_active && g_state == ScreenSaverState::Awake) {
					if (displayManager) displayManager->restoreTransientScreen();
					g_idle_screen_active = false;
					LOGI("SAVER", "Idle Screen restored");
					return;
				}
				if (g_idle_screen_active && g_state == ScreenSaverState::FadingOut) {
					if (displayManager) displayManager->restoreTransientScreen();
					g_idle_screen_active = false;
				}
				if (g_idle_screen_active && g_state == ScreenSaverState::Asleep) {
					if (displayManager) displayManager->restoreTransientScreen();
					g_idle_screen_active = false;
				}
				if (g_idle_screen_active && g_state == ScreenSaverState::FadingIn) {
					if (displayManager) displayManager->restoreTransientScreen();
					g_idle_screen_active = false;
				}
				const uint8_t target = config_brightness();
				const uint8_t from = g_current_brightness;

				// Already awake at target brightness; nothing to do.
				if (g_state == ScreenSaverState::Awake && from == target) {
						return;
				}

				// Remove sleep overlay before fade-in so the screen is visible.
				remove_sleep_overlay();

				// Apply the new pixel shift offset to the active screen.
				int dx, dy;
				screen_saver_manager_get_pixel_shift(&dx, &dy);
				if (displayManager) {
						displayManager->lock();
						lv_obj_t* scr = lv_scr_act();
						if (scr) {
								lv_obj_set_style_translate_x(scr, dx, 0);
								lv_obj_set_style_translate_y(scr, dy, 0);
						}
						displayManager->unlock();
				}

				#if HAS_TOUCH
				// Swallow wake interactions so swipe-to-wake doesn't click through into LVGL.
				// We suppress for (fade_in + small buffer) and only when waking from sleep/dimming.
				if (g_state == ScreenSaverState::Asleep || g_state == ScreenSaverState::FadingOut) {
						const uint32_t windowMs = (uint32_t)fade_in_ms() + 250;
						touch_manager_suppress_lvgl_input(windowMs);
				}
				#endif

				// Wake panel in two phases so the mandatory 120 ms DCS delay between
				// Sleep Out (0x11) and Display On (0x29) does not block the display lock.
				// Holding the lock across delay() would stall LVGL flush for 120 ms.
				// Drivers that complete wake in a single step (e.g. hard-reset wake that
				// replays the full init sequence) report needsTwoPhaseWake()==false so the
				// gap and second lock acquisition are skipped.
				if (g_state == ScreenSaverState::Asleep) {
						if (displayManager && displayManager->getDriver()) {
								DisplayDriver* drv = displayManager->getDriver();
								const bool twoPhase = drv->needsTwoPhaseWake();
								displayManager->lock();
								drv->displayWakeSleepOut();
								displayManager->unlock();
								if (twoPhase) {
										// DCS spec: ≥120 ms between Sleep Out and Display On.
										// vTaskDelay yields to other tasks during the wait.
										vTaskDelay(pdMS_TO_TICKS(120));
										displayManager->lock();
										drv->displayWakeDisplayOn();
										displayManager->unlock();
								}
						}
				}

				start_fade(ScreenSaverState::FadingIn, from, target, fade_in_ms());
				LOGI("SAVER", "Wake requested (pixel shift dx=%d dy=%d)", dx, dy);
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
				if (g_fade_to == 0) complete_fade_out();
				else enter_awake();
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
		// Both Idle Screen and Display Sleep only transition from an awake panel.
		if (g_state != ScreenSaverState::Awake) return;

		const uint32_t now = millis();
		const uint32_t elapsed = now - g_last_activity_ms;
		const ScreenSaverScheduleAction action = screen_saver_schedule_action(
				is_enabled(), timeout_ms(), idle_screen_enabled(), g_idle_screen_active,
				g_idle_screen_attempted, g_config->idle_screen_pad[0] != '\0',
				idle_screen_timeout_ms(), elapsed);

		if (action == ScreenSaverScheduleAction::ShowIdleScreen) {
				g_idle_screen_attempted = true;
				if (displayManager && displayManager->showTransientScreen(g_config->idle_screen_pad)) {
					g_idle_screen_active = true;
					LOGI("SAVER", "Idle Screen: %s", g_config->idle_screen_pad);
				} else {
					LOGW("SAVER", "Idle Screen unavailable: %s", g_config->idle_screen_pad);
				}
		}

		if (action == ScreenSaverScheduleAction::Sleep) {
				start_fade(ScreenSaverState::FadingOut, g_current_brightness, 0, fade_out_ms());
				LOGI("SAVER", "Auto-sleep (timeout)");
		}
}

// Periodic scrub while fully asleep. Drivers use this hook to re-blank
// framebuffers or otherwise refresh state to mitigate image retention /
// VCOM drift on IPS panels during multi-hour idle. SCREENSAVER_SLEEP_REFRESH_MS
// of 0 disables the periodic refresh entirely.
static void maybe_refresh_asleep() {
#if SCREENSAVER_SLEEP_REFRESH_MS > 0
		if (g_state != ScreenSaverState::Asleep) return;
		if (!displayManager || !displayManager->getDriver()) return;

		const uint32_t now = millis();
		if (now - g_last_sleep_refresh_ms < SCREENSAVER_SLEEP_REFRESH_MS) return;

		g_last_sleep_refresh_ms = now;
		displayManager->lock();
		displayManager->getDriver()->displayRefreshSleep();
		displayManager->unlock();
		LOGI("SAVER", "Periodic sleep refresh");
#endif
}

#if HAS_TOUCH
static void poll_touch_activity() {
		if (!g_config) return;
		if (!g_config->screen_saver_wake_on_touch) return;

		// Avoid competing with LVGL's indev polling while awake.
		// Only poll the raw touch state to wake the backlight when sleeping/dimming.
		if ((g_state == ScreenSaverState::Awake && !g_idle_screen_active) || g_state == ScreenSaverState::FadingIn) return;

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
		g_idle_screen_active = false;
		g_idle_screen_attempted = false;

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
		maybe_refresh_asleep();

		#if HAS_TOUCH
		// While dimming/asleep/fading in, suppress LVGL input so wake gestures don't click-through.
		// This is based on state (not config enabled), so it also protects transitions caused
		// by explicit API calls.
		static bool prev_force = false;
		const bool force = (g_state != ScreenSaverState::Awake || g_idle_screen_active);
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

void screen_saver_manager_set_brightness(uint8_t brightness) {
		if (brightness > 100) brightness = 100;
		request_brightness(brightness);
}

bool screen_saver_manager_is_asleep() {
		return g_state == ScreenSaverState::Asleep || g_state == ScreenSaverState::FadingOut;
}

bool screen_saver_manager_is_fully_asleep() {
		return g_state == ScreenSaverState::Asleep;
}

bool screen_saver_manager_input_ready() {
		return g_state.load() == ScreenSaverState::Awake;
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

void screen_saver_manager_get_pixel_shift(int* dx, int* dy) {
		const uint8_t distance = button_defaults_get_pixel_shift_distance();
		if (distance == 0) {
			if (dx) *dx = 0;
			if (dy) *dy = 0;
			return;
		}
		const uint16_t side = 2 * distance + 1;
		int col = (int)(g_pixel_shift_counter % side) - distance;
		int row = (int)(g_pixel_shift_counter / side) - distance;
		if (dx) *dx = col;
		if (dy) *dy = row;
}

#endif // HAS_DISPLAY

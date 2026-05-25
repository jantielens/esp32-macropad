#include "power_manager.h"

#include "board_config.h"
#include "config_manager.h"
#include "log_manager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <Preferences.h>

// RTC-retained backoff state
RTC_DATA_ATTR static uint32_t g_wifi_backoff_seconds = 0;
RTC_DATA_ATTR static uint8_t g_wifi_fail_count = 0;
#if HAS_EPAPER_WAKE_BUTTON
static EpaperButtonWakeAction g_button_wake_action_boot = EpaperButtonWakeAction::None;
#endif

static bool g_is_deep_sleep_wake = false;
static PowerMode g_boot_mode = PowerMode::AlwaysOn;
static PowerMode g_current_mode = PowerMode::AlwaysOn;
static bool g_force_config_mode = false;

#if HAS_EPAPER_WAKE_BUTTON
static EpaperButtonWakeAction classify_button_wake(uint32_t threshold_ms) {
		if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0) {
				return EpaperButtonWakeAction::None;
		}

		pinMode(EPAPER_BUTTON_PIN, INPUT);
		const unsigned long start = millis();
		while (digitalRead(EPAPER_BUTTON_PIN) == LOW) {
				if ((millis() - start) >= threshold_ms) {
						return EpaperButtonWakeAction::Config;
				}
			delay(10);
		}

		return EpaperButtonWakeAction::Refresh;
}
#endif

static const uint8_t kPoweronBurstRequired = 2;
static const unsigned long kPoweronBurstWindowMs = 10000;
#if POWERON_CONFIG_BURST_ENABLED
static bool g_poweron_burst_pending_clear = false;
static unsigned long g_poweron_burst_boot_ms = 0;
#endif

static void led_write(bool on) {
#if HAS_BUILTIN_LED
		digitalWrite(LED_PIN, LED_ACTIVE_HIGH ? (on ? HIGH : LOW) : (on ? LOW : HIGH));
#else
		(void)on;
#endif
}

void power_manager_boot_init() {
		const esp_reset_reason_t reason = esp_reset_reason();
		g_is_deep_sleep_wake = (reason == ESP_RST_DEEPSLEEP);
		g_force_config_mode = false;
		#if HAS_EPAPER_WAKE_BUTTON
		g_button_wake_action_boot = classify_button_wake(2500);
		#endif

		#if POWERON_CONFIG_BURST_ENABLED
		// Power-on burst detection (for boards without a reliable user button):
		// - Counts consecutive ESP_RST_POWERON boots.
		// - If a second power-on happens within ~10s uptime (before clear), force Config Mode.
		// - Counter is cleared after 10s of uptime.
		// - Deep sleep and other reset reasons do not affect the counter.
		g_poweron_burst_pending_clear = false;
		g_poweron_burst_boot_ms = millis();

		if (reason == ESP_RST_POWERON) {
				Preferences prefs;
				if (prefs.begin("power_burst", false)) {
						uint8_t count = prefs.getUChar("pwr_cnt", 0);
						if (count >= kPoweronBurstRequired) {
								count = 0;
						}

						count++;
						if (count >= kPoweronBurstRequired) {
								g_force_config_mode = true;
								prefs.putUChar("pwr_cnt", 0);
								LOGI("Power", "Power-on burst detected; entering Config Mode");
						} else {
								prefs.putUChar("pwr_cnt", count);
								g_poweron_burst_pending_clear = true;
								LOGI("Power", "Power-on burst count: %u/%u", (unsigned)count, (unsigned)kPoweronBurstRequired);
						}
						prefs.end();
				}
		}
		#endif
}

void power_manager_configure(const DeviceConfig *config, bool config_loaded, bool force_config_mode) {
		if (force_config_mode) {
				g_boot_mode = PowerMode::Config;
				g_current_mode = g_boot_mode;
				return;
		}

		if (!config_loaded) {
				g_boot_mode = PowerMode::Ap;
				g_current_mode = g_boot_mode;
				return;
		}

		g_boot_mode = power_config_parse_power_mode(config);
		g_current_mode = g_boot_mode;
}

PowerMode power_manager_get_boot_mode() {
		return g_boot_mode;
}

PowerMode power_manager_get_current_mode() {
		return g_current_mode;
}

void power_manager_set_current_mode(PowerMode mode) {
		g_current_mode = mode;
}

bool power_manager_should_force_config_mode() {
		return g_force_config_mode;
}

bool power_manager_is_deep_sleep_wake() {
		return g_is_deep_sleep_wake;
}

bool power_manager_should_publish_mqtt_discovery() {
		// Allow discovery on cold boot or explicit Config Mode.
		if (!g_is_deep_sleep_wake) return true;
		return g_current_mode == PowerMode::Config;
}

void power_manager_note_wifi_success() {
		g_wifi_fail_count = 0;
		g_wifi_backoff_seconds = 0;
}

uint32_t power_manager_note_wifi_failure(uint32_t base_seconds, uint32_t max_seconds) {
		if (base_seconds == 0) base_seconds = 1;
		if (max_seconds == 0) max_seconds = base_seconds;

		if (g_wifi_backoff_seconds == 0) {
				g_wifi_backoff_seconds = base_seconds;
		} else {
				uint32_t next = g_wifi_backoff_seconds * 2U;
				g_wifi_backoff_seconds = next;
		}

		if (g_wifi_backoff_seconds > max_seconds) {
				g_wifi_backoff_seconds = max_seconds;
		}

		g_wifi_fail_count++;

		LOGW("Power", "WiFi backoff: %us (fail_count=%u)", (unsigned)g_wifi_backoff_seconds, (unsigned)g_wifi_fail_count);
		return g_wifi_backoff_seconds;
}

uint32_t power_manager_get_wifi_backoff_seconds() {
		return g_wifi_backoff_seconds;
}

void power_manager_sleep_for(uint32_t seconds) {
		// E-paper button-only mode: when wake_seconds is 0 AND we're in the
		// DutyCycleEpaper mode, the device sleeps with ONLY the ext0 (wake
		// button) source armed -- no timer wakeup. Other duty-cycle modes
		// retain the "0 = wake immediately" semantics via the clamp below.
		const bool epaper_button_only =
#if HAS_EPAPER
				(power_manager_get_current_mode() == PowerMode::DutyCycleEpaper && seconds == 0);
#else
				false;
#endif

		if (!epaper_button_only && seconds == 0) {
				seconds = 1;
		}

		if (epaper_button_only) {
				LOGI("Power", "Sleeping (button-only, no timer wakeup)");
		} else {
				LOGI("Power", "Sleeping for %us", (unsigned)seconds);
		}

		led_write(false);

		WiFi.disconnect(true);
		WiFi.mode(WIFI_OFF);

		if (!epaper_button_only) {
				esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
		}
#if HAS_EPAPER_WAKE_BUTTON
		// Inkplate WAKE button is active-low on a RTC GPIO; arm ext0 alongside the timer.
		esp_sleep_enable_ext0_wakeup((gpio_num_t)EPAPER_BUTTON_PIN, 0);
#endif
		esp_deep_sleep_start();
}

#if HAS_EPAPER_WAKE_BUTTON
bool power_manager_is_button_wake() {
		return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
}

EpaperButtonWakeAction power_manager_get_button_wake_action() {
		return g_button_wake_action_boot;
}
#endif

void power_manager_loop() {
		#if POWERON_CONFIG_BURST_ENABLED
		if (g_poweron_burst_pending_clear) {
				const unsigned long now = millis();
				if (g_poweron_burst_boot_ms == 0) {
						g_poweron_burst_boot_ms = now;
						return;
				}

				if ((now - g_poweron_burst_boot_ms) >= kPoweronBurstWindowMs) {
						Preferences prefs;
						if (prefs.begin("power_burst", false)) {
								prefs.putUChar("pwr_cnt", 0);
								prefs.end();
						}
						g_poweron_burst_pending_clear = false;
						LOGI("Power", "Power-on burst window expired; clearing counter");
				}
		}
		#endif
}

// LED behavior
static unsigned long g_led_last_toggle_ms = 0;
static unsigned long g_led_interval_ms = 0;
static bool g_led_state = false;

void power_manager_led_set_mode(PowerMode mode) {
#if HAS_BUILTIN_LED
		switch (mode) {
				case PowerMode::DutyCycle:
				case PowerMode::DutyCycleBle:
						g_led_interval_ms = 0;
						g_led_state = true;
						led_write(true);
						break;
				case PowerMode::Config:
						g_led_interval_ms = 500; // 1 Hz blink
						g_led_last_toggle_ms = millis();
						break;
				case PowerMode::Ap:
						g_led_interval_ms = 125; // 4 Hz blink
						g_led_last_toggle_ms = millis();
						break;
				default:
						g_led_interval_ms = 0;
						g_led_state = false;
						led_write(false);
						break;
		}
#else
		(void)mode;
#endif
}

void power_manager_led_loop() {
#if HAS_BUILTIN_LED
		if (g_led_interval_ms == 0) return;

		const unsigned long now = millis();
		if ((now - g_led_last_toggle_ms) >= g_led_interval_ms) {
				g_led_last_toggle_ms = now;
				g_led_state = !g_led_state;
				led_write(g_led_state);
		}
#endif
}

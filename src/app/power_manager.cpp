#include "power_manager.h"
#include "time_service.h"

#include "board_config.h"
#include "config_manager.h"
#include "device_class.h"
#include "log_manager.h"
#include "ota_activity.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>
#include <atomic>

// RTC-retained backoff state
RTC_DATA_ATTR static uint32_t g_wifi_backoff_seconds = 0;
RTC_DATA_ATTR static uint8_t g_wifi_fail_count = 0;

static bool g_is_deep_sleep_wake = false;
static PowerMode g_boot_mode = PowerMode::AlwaysOn;
static PowerMode g_current_mode = PowerMode::AlwaysOn;
static bool g_force_config_mode = false;
static bool g_force_setup_ap = false;

#if POWERON_CONFIG_BURST_ENABLED
static constexpr uint64_t kPoweronBurstWindowUs = 10000000ULL;
static SemaphoreHandle_t g_power_prefs_mutex = nullptr;
static esp_timer_handle_t g_poweron_burst_timer = nullptr;
static std::atomic<bool> g_poweron_burst_pending_clear{false};

static bool clear_poweron_burst_marker() {
		if (!g_power_prefs_mutex ||
				xSemaphoreTake(g_power_prefs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
				return false;
		}
		Preferences prefs;
		const bool opened = prefs.begin("power_burst", false);
		const bool cleared = opened && prefs.putBool("armed", false) > 0;
		if (opened) prefs.end();
		xSemaphoreGive(g_power_prefs_mutex);
		if (!cleared) LOGE("Power", "Failed to clear power-on recovery marker");
		return cleared;
}

static void poweron_burst_expired(void*) {
		g_poweron_burst_pending_clear.store(true, std::memory_order_release);
		if (!ota_activity_try_begin_config_write()) return;
		if (clear_poweron_burst_marker()) {
				g_poweron_burst_pending_clear.store(false, std::memory_order_release);
				LOGI("Power", "Power-on recovery window expired");
		}
		ota_activity_finish_config_write();
}

static bool arm_poweron_burst_marker() {
		if (!g_power_prefs_mutex ||
				xSemaphoreTake(g_power_prefs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
				LOGE("Power", "Could not lock NVS for power-on recovery marker");
				return false;
		}
		Preferences prefs;
		const bool opened = prefs.begin("power_burst", false);
		const bool armed = opened && prefs.putBool("armed", true) > 0;
		if (opened) prefs.end();
		xSemaphoreGive(g_power_prefs_mutex);
		if (!armed) {
				LOGE("Power", "Failed to arm power-on recovery marker");
				return false;
		}

		if (!g_poweron_burst_timer) {
				esp_timer_create_args_t timer_args = {};
				timer_args.callback = poweron_burst_expired;
				timer_args.dispatch_method = ESP_TIMER_TASK;
				timer_args.name = "pwr_recovery";
				if (esp_timer_create(&timer_args, &g_poweron_burst_timer) != ESP_OK) {
						LOGE("Power", "Failed to create power-on recovery timer");
						(void)clear_poweron_burst_marker();
						return false;
				}
		}
		if (esp_timer_start_once(g_poweron_burst_timer, kPoweronBurstWindowUs) != ESP_OK) {
				LOGE("Power", "Failed to start power-on recovery timer");
				(void)clear_poweron_burst_marker();
				return false;
		}
		return true;
}
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
		g_force_setup_ap = false;

		// Allow registered device classes to classify the wake reason and
		// optionally request Config Mode. OR'd with any contribution from the
		// power-on burst detector below so neither path can mask the other.
		{
				bool dc_force_config = false;
				(void)device_class_dispatch_wake_classify(&dc_force_config);
				if (dc_force_config) g_force_config_mode = true;
		}

		Preferences prefs;
		bool opened = false;
		bool requested_ap = false;
		#if POWERON_CONFIG_BURST_ENABLED
		bool recovery_armed = false;
		bool need_recovery_arm = false;
		if (!g_power_prefs_mutex) g_power_prefs_mutex = xSemaphoreCreateMutex();
		if (g_power_prefs_mutex &&
				xSemaphoreTake(g_power_prefs_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
				opened = prefs.begin("power_burst", false);
				if (opened) {
						recovery_armed = prefs.getBool("armed", false);
						requested_ap = prefs.getBool("req_ap", false);
						if (requested_ap && prefs.putBool("req_ap", false) == 0) {
								LOGE("Power", "Failed to consume setup AP restart request");
								requested_ap = false;
						}
						if (reason == ESP_RST_POWERON) {
								if (recovery_armed) {
										if (prefs.putBool("armed", false) > 0) {
												g_force_setup_ap = true;
												LOGI("Power", "Power-on recovery detected; entering setup AP");
										} else {
												LOGE("Power", "Failed to consume power-on recovery marker");
										}
								} else {
										need_recovery_arm = true;
								}
						} else if (recovery_armed) {
								if (prefs.putBool("armed", false) == 0) {
										LOGE("Power", "Failed to clear marker after non-power-on reset");
								}
						}
						prefs.end();
				}
				xSemaphoreGive(g_power_prefs_mutex);
		} else {
				LOGE("Power", "Could not lock NVS during boot recovery check");
		}
		#else
		opened = prefs.begin("power_burst", false);
		if (opened) {
				requested_ap = prefs.getBool("req_ap", false);
				if (requested_ap && prefs.putBool("req_ap", false) == 0) {
						LOGE("Power", "Failed to consume setup AP restart request");
						requested_ap = false;
				}
				prefs.end();
		}
		#endif
		if (opened && requested_ap) {
				if (reason == ESP_RST_SW) {
						g_force_setup_ap = true;
						LOGI("Power", "One-shot setup AP restart requested");
				} else {
						LOGW("Power", "Ignoring setup AP request on reset reason %d", static_cast<int>(reason));
				}
		}
		#if POWERON_CONFIG_BURST_ENABLED
		if (need_recovery_arm && !g_force_setup_ap) {
				if (arm_poweron_burst_marker()) {
						LOGI("Power", "Power-on recovery window armed");
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

bool power_manager_should_force_setup_ap() {
		return g_force_setup_ap;
}

bool power_manager_request_setup_ap_restart() {
		if (!ota_activity_try_begin_network_transition()) {
				LOGW("Power", "Setup AP restart rejected while an update or configuration write is active");
				return false;
		}
		#if POWERON_CONFIG_BURST_ENABLED
		if (!g_power_prefs_mutex) g_power_prefs_mutex = xSemaphoreCreateMutex();
		if (!g_power_prefs_mutex ||
				xSemaphoreTake(g_power_prefs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
				LOGE("Power", "Could not lock NVS for setup AP restart request");
				ota_activity_finish_network_transition();
				return false;
		}
		#endif
		Preferences prefs;
		const bool opened = prefs.begin("power_burst", false);
		const bool requested = opened && prefs.putBool("req_ap", true) > 0;
		if (opened) prefs.end();
		#if POWERON_CONFIG_BURST_ENABLED
		xSemaphoreGive(g_power_prefs_mutex);
		#endif
		if (!requested) {
				LOGE("Power", "Failed to persist setup AP restart request");
				ota_activity_finish_network_transition();
		}
		return requested;
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
		// Let registered device classes mutate the sleep duration and arm any
		// additional wake sources they own (ext0/ext1/touchpad/etc.). Hook is
		// a no-op until at least one class registers.
		device_class_dispatch_sleep_prepare(&seconds);

		// A registered device class may opt into button-only sleep by leaving
		// seconds at 0 (its sleep_prepare hook is responsible for arming the
		// alternate wake source). Without a registered class, treat 0 as
		// "wake immediately" by clamping to 1 second.
		const bool class_owns_wake =
				(device_class_find_by_mode(power_manager_get_current_mode()) != nullptr);
		if (seconds == 0 && !class_owns_wake) {
				seconds = 1;
		}

		if (seconds == 0) {
				LOGI("Power", "Sleeping (button-only, no timer wakeup; wake time depends on external trigger)");
		} else {
				time_t now = time(nullptr);
				time_t wake_epoch = now + (time_t)seconds;

				if (now >= 946684800) {
						struct tm wake_tm;
						char wake_buf[32];
						time_service_localtime(wake_epoch, &wake_tm);
						strftime(wake_buf, sizeof(wake_buf), "%Y-%m-%d %H:%M:%S", &wake_tm);
						LOGI("Power", "Sleeping for %us; planned wake at %s (epoch=%llu)",
						     (unsigned)seconds,
						     wake_buf,
						     (unsigned long long)wake_epoch);
				} else {
						LOGI("Power", "Sleeping for %us; planned wake in +%us (clock not synced)",
						     (unsigned)seconds,
						     (unsigned)seconds);
				}
		}

#if POWERON_CONFIG_BURST_ENABLED
		if (g_poweron_burst_timer) (void)esp_timer_stop(g_poweron_burst_timer);
		g_poweron_burst_pending_clear.store(false, std::memory_order_release);
		(void)clear_poweron_burst_marker();
#endif

		led_write(false);

		WiFi.disconnect(true);
		WiFi.mode(WIFI_OFF);

		if (seconds > 0) {
				esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
		}
		esp_deep_sleep_start();
}

void power_manager_loop() {
		#if POWERON_CONFIG_BURST_ENABLED
		if (g_poweron_burst_pending_clear.load(std::memory_order_acquire) &&
				ota_activity_try_begin_config_write()) {
				if (clear_poweron_burst_marker()) {
						g_poweron_burst_pending_clear.store(false, std::memory_order_release);
						LOGI("Power", "Power-on recovery window expired");
				}
				ota_activity_finish_config_write();
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

// E-paper DeviceClass implementation.
//
// All e-paper-specific lifecycle (NVS load/save, REST API, wake button
// classification, boot splashes, duty cycle, MQTT discovery) lives in this
// translation unit. Aggregated into the build via device_classes.cpp under
// the HAS_EPAPER gate. The core firmware never touches e-paper symbols
// directly; it dispatches through the DeviceClass registry.

#include "board_config.h"

#if HAS_EPAPER

#include "epaper/epaper_config.h"

#include "config_manager.h"
#include "device_class.h"
#include "epaper/epaper_battery.h"
#include "epaper/epaper_driver.h"
#include "epaper/epaper_refresh.h"
#include "epaper/epaper_screens.h"
#include "epaper/epaper_timing.h"
#include "log_manager.h"
#include "power_config.h"
#include "power_manager.h"
#include "version.h"
#include "wifi_manager.h"

#if HAS_MQTT
#include "epaper/epaper_mqtt.h"
#include "mqtt_manager.h"
extern MqttManager mqtt_manager;
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <time.h>
#if HAS_EPAPER_FRONTLIGHT
#include <esp_task_wdt.h>
#endif

// Global e-paper config instance (declared extern in epaper_config.h).
EpaperConfig g_epaper_config = {};

// ---------------------------------------------------------------------------
// NVS keys. Each is <= 15 chars (NVS limit). Kept identical to the keys the
// core used to write before the Phase 2 split so existing devices keep their
// stored values across the upgrade.
// ---------------------------------------------------------------------------
static const char *kNvsNamespace      = "device_cfg";
static const char *kKeyUrl            = "ep_url";
static const char *kKeyRotation       = "ep_rot";
static const char *kKeyCrc32          = "ep_crc32";
static const char *kKeyOverlayEn      = "ep_ovl_en";
static const char *kKeyOverlayPos     = "ep_ovl_pos";
static const char *kKeyOverlayCol     = "ep_ovl_col";
static const char *kKeyOverlayItems   = "ep_ovl_it";
static const char *kKeyFrontBright    = "ep_fl_b";
static const char *kKeyFrontDuration  = "ep_fl_d";

// ---------------------------------------------------------------------------
// Wake-button classification (formerly in power_manager.cpp).
// ---------------------------------------------------------------------------
#if HAS_EPAPER_WAKE_BUTTON
static EpaperButtonWakeAction g_button_wake_action_boot = EpaperButtonWakeAction::None;

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

static bool classify_cold_boot_config_hold(uint32_t threshold_ms) {
		if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
				return false;
		}

		// On cold boot the wake button doubles as the "enter Config Mode"
		// button. Keep parity with pre-refactor behavior: a sustained hold
		// enters Config Mode even when HAS_BUTTON is false on e-paper boards.
		pinMode(EPAPER_BUTTON_PIN, INPUT);
		const unsigned long start = millis();
		while ((millis() - start) < threshold_ms) {
				if (digitalRead(EPAPER_BUTTON_PIN) != LOW) return false;
				delay(10);
		}
		return true;
}

bool epaper_button_is_button_wake() {
		return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
}

EpaperButtonWakeAction epaper_button_wake_action() {
		return g_button_wake_action_boot;
}
#else
bool epaper_button_is_button_wake() { return false; }
EpaperButtonWakeAction epaper_button_wake_action() { return EpaperButtonWakeAction::None; }
#endif

// Single-key CRC persist so the refresh pipeline doesn't have to rewrite the
// entire config (and wear NVS) on every successful redraw.
void epaper_config_persist_crc(uint32_t crc) {
		Preferences prefs;
		if (!prefs.begin(kNvsNamespace, false)) {
				LOGW("EpaperCfg", "CRC persist: NVS open failed");
				return;
		}
		prefs.putUInt(kKeyCrc32, crc);
		prefs.end();
}

// ---------------------------------------------------------------------------
// Config hooks (defaults / load / save / API).
// ---------------------------------------------------------------------------
static void config_defaults_hook(DeviceConfig * /*cfg*/) {
		g_epaper_config.epaper_url[0] = '\0';
		g_epaper_config.epaper_rotation = 0;
		g_epaper_config.epaper_last_crc32 = 0;
		g_epaper_config.epaper_overlay_enabled = false;
		g_epaper_config.epaper_overlay_position = 3;
		g_epaper_config.epaper_overlay_color = 0;
		g_epaper_config.epaper_overlay_items = 0x7; // batt-icon | batt-% | time
		g_epaper_config.epaper_frontlight_brightness = 0;
		g_epaper_config.epaper_frontlight_duration_s = 30;
}

static void config_load_hook(DeviceConfig * /*cfg*/, Preferences &prefs) {
		// Defaults so missing keys land on sane values.
		config_defaults_hook(nullptr);

		String url = prefs.getString(kKeyUrl, "");
		strlcpy(g_epaper_config.epaper_url, url.c_str(), CONFIG_EPAPER_URL_MAX_LEN);
		g_epaper_config.epaper_rotation = prefs.getUChar(kKeyRotation, 0);
		if (g_epaper_config.epaper_rotation > 3) g_epaper_config.epaper_rotation = 0;
		g_epaper_config.epaper_last_crc32 = prefs.getUInt(kKeyCrc32, 0);

		g_epaper_config.epaper_overlay_enabled = prefs.getBool(kKeyOverlayEn, false);
		g_epaper_config.epaper_overlay_position = prefs.getUChar(kKeyOverlayPos, 3);
		if (g_epaper_config.epaper_overlay_position > 3) g_epaper_config.epaper_overlay_position = 3;
		g_epaper_config.epaper_overlay_color = prefs.getUChar(kKeyOverlayCol, 0);
		if (g_epaper_config.epaper_overlay_color > 3) g_epaper_config.epaper_overlay_color = 0;
		g_epaper_config.epaper_overlay_items = prefs.getUChar(kKeyOverlayItems, 0x7);

		g_epaper_config.epaper_frontlight_brightness = prefs.getUChar(kKeyFrontBright, 0);
		if (g_epaper_config.epaper_frontlight_brightness > 63) g_epaper_config.epaper_frontlight_brightness = 63;
		g_epaper_config.epaper_frontlight_duration_s = prefs.getUShort(kKeyFrontDuration, 30);
}

static void config_save_hook(const DeviceConfig * /*cfg*/, Preferences &prefs) {
		prefs.putString(kKeyUrl, g_epaper_config.epaper_url);
		prefs.putUChar(kKeyRotation, g_epaper_config.epaper_rotation);
		prefs.putUInt(kKeyCrc32, g_epaper_config.epaper_last_crc32);
		prefs.putBool(kKeyOverlayEn, g_epaper_config.epaper_overlay_enabled);
		prefs.putUChar(kKeyOverlayPos, g_epaper_config.epaper_overlay_position);
		prefs.putUChar(kKeyOverlayCol, g_epaper_config.epaper_overlay_color);
		prefs.putUChar(kKeyOverlayItems, g_epaper_config.epaper_overlay_items);
		prefs.putUChar(kKeyFrontBright, g_epaper_config.epaper_frontlight_brightness);
		prefs.putUShort(kKeyFrontDuration, g_epaper_config.epaper_frontlight_duration_s);
}

static void config_api_get_hook(const DeviceConfig * /*cfg*/, JsonObject &root) {
		root["caps"]["epaper"] = true;
		root["epaper_url"] = g_epaper_config.epaper_url;
		root["epaper_rotation"] = g_epaper_config.epaper_rotation;
		root["epaper_overlay_enabled"] = g_epaper_config.epaper_overlay_enabled;
		root["epaper_overlay_position"] = g_epaper_config.epaper_overlay_position;
		root["epaper_overlay_color"] = g_epaper_config.epaper_overlay_color;
		root["epaper_overlay_items"] = g_epaper_config.epaper_overlay_items;
		root["epaper_frontlight_brightness"] = g_epaper_config.epaper_frontlight_brightness;
		root["epaper_frontlight_duration_s"] = g_epaper_config.epaper_frontlight_duration_s;
		root["epaper_frontlight_supported"] = (bool)HAS_EPAPER_FRONTLIGHT;
}

static void config_api_set_hook(DeviceConfig * /*cfg*/, JsonObject &body) {
		if (body.containsKey("epaper_url")) {
				const char *v = body["epaper_url"] | "";
				strlcpy(g_epaper_config.epaper_url, v, CONFIG_EPAPER_URL_MAX_LEN);
		}
		if (body.containsKey("epaper_rotation")) {
				uint8_t v = body["epaper_rotation"].is<const char*>()
						? (uint8_t)atoi(body["epaper_rotation"].as<const char*>())
						: (uint8_t)(body["epaper_rotation"] | 0);
				if (v > 3) v = 0;
				g_epaper_config.epaper_rotation = v;
		}
		if (body.containsKey("epaper_overlay_enabled")) {
				g_epaper_config.epaper_overlay_enabled = body["epaper_overlay_enabled"] | false;
		}
		if (body.containsKey("epaper_overlay_position")) {
				uint8_t v = body["epaper_overlay_position"].is<const char*>()
						? (uint8_t)atoi(body["epaper_overlay_position"].as<const char*>())
						: (uint8_t)(body["epaper_overlay_position"] | 3);
				if (v > 3) v = 3;
				g_epaper_config.epaper_overlay_position = v;
		}
		if (body.containsKey("epaper_overlay_color")) {
				uint8_t v = body["epaper_overlay_color"].is<const char*>()
						? (uint8_t)atoi(body["epaper_overlay_color"].as<const char*>())
						: (uint8_t)(body["epaper_overlay_color"] | 0);
				if (v > 3) v = 0;
				g_epaper_config.epaper_overlay_color = v;
		}
		if (body.containsKey("epaper_overlay_items")) {
				g_epaper_config.epaper_overlay_items = body["epaper_overlay_items"].is<const char*>()
						? (uint8_t)atoi(body["epaper_overlay_items"].as<const char*>())
						: (uint8_t)(body["epaper_overlay_items"] | 0x7);
		}
		if (body.containsKey("epaper_frontlight_brightness")) {
				uint8_t v = body["epaper_frontlight_brightness"].is<const char*>()
						? (uint8_t)atoi(body["epaper_frontlight_brightness"].as<const char*>())
						: (uint8_t)(body["epaper_frontlight_brightness"] | 0);
				if (v > 63) v = 63;
				g_epaper_config.epaper_frontlight_brightness = v;
		}
		if (body.containsKey("epaper_frontlight_duration_s")) {
				g_epaper_config.epaper_frontlight_duration_s = body["epaper_frontlight_duration_s"].is<const char*>()
						? (uint16_t)atoi(body["epaper_frontlight_duration_s"].as<const char*>())
						: (uint16_t)(body["epaper_frontlight_duration_s"] | 30);
		}
}

// ---------------------------------------------------------------------------
// Wake classification + sleep prep hooks.
// ---------------------------------------------------------------------------
static void wake_classify_hook(bool *handled, bool *force_config) {
#if HAS_EPAPER_WAKE_BUTTON
		g_button_wake_action_boot = classify_button_wake(2500);
		const bool cold_boot_hold_config = classify_cold_boot_config_hold(2500);
		if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
				if (handled) *handled = true;
		}
		if (g_button_wake_action_boot == EpaperButtonWakeAction::Config || cold_boot_hold_config) {
				g_button_wake_action_boot = EpaperButtonWakeAction::Config;
				if (cold_boot_hold_config) {
						LOGI("Power", "E-paper wake button held on cold boot - entering Config Mode");
				}
				if (force_config) *force_config = true;
		}
#else
		(void)handled;
		(void)force_config;
#endif
}

static void sleep_prepare_hook(uint32_t *seconds_inout) {
		// E-paper button-only mode: when wake_seconds is 0, keep it at 0 so
		// power_manager_sleep_for() can skip timer wake and rely on class-owned
		// wake sources (ext0 here). The core clamps 0->1 only when no class owns
		// the active power mode.
		(void)seconds_inout;
#if HAS_EPAPER_WAKE_BUTTON
		esp_sleep_enable_ext0_wakeup((gpio_num_t)EPAPER_BUTTON_PIN, 0);
#endif
}

// ---------------------------------------------------------------------------
// Setup hooks: ack splash and post-WiFi config-mode screen.
// ---------------------------------------------------------------------------
static void on_setup_early_hook(DeviceConfig * /*cfg*/, PowerMode boot_mode) {
		// Immediate ack when entering Config / AP mode so the user sees their
		// long-press / reset-burst was registered without waiting for Wi-Fi.
		if (EPAPER_FAST_REFRESH && (boot_mode == PowerMode::Config || boot_mode == PowerMode::Ap)) {
				epaper_show_status(g_epaper_config.epaper_rotation, []() {
						epaper_screen_config_mode_starting();
				});
		}
}

static void on_setup_late_hook(DeviceConfig *cfg, PowerMode current_mode) {
		if (current_mode != PowerMode::Config && current_mode != PowerMode::Ap) return;
		const bool is_ap = (current_mode == PowerMode::Ap);
		epaper_show_status(g_epaper_config.epaper_rotation, [is_ap, cfg]() {
				if (is_ap) {
						const String ip = WiFi.softAPIP().toString();
						const String ssid = WiFi.softAPSSID();
						epaper_screen_config_mode(ssid.c_str(), ip.c_str(), true);
				} else {
						const String ip = WiFi.localIP().toString();
						epaper_screen_config_mode(cfg->wifi_ssid, ip.c_str(), false);
				}
		});
}

// ---------------------------------------------------------------------------
// Loop hook: poll wake button while in Config / AP and reboot on press.
// ---------------------------------------------------------------------------
#if HAS_EPAPER_WAKE_BUTTON
static void on_loop_hook() {
		static bool s_armed = false;
		static unsigned long s_arm_at_ms = 0;
		static bool s_last_released = true;

		const PowerMode now_mode = power_manager_get_current_mode();
		const bool in_config = (now_mode == PowerMode::Config || now_mode == PowerMode::Ap);
		if (!in_config) {
				s_armed = false;
				return;
		}

		if (!s_armed) {
				pinMode(EPAPER_BUTTON_PIN, INPUT);
				s_arm_at_ms = millis() + 1500;
				s_last_released = true;
				s_armed = true;
				return;
		}
		if ((long)(millis() - s_arm_at_ms) < 0) return;

		const bool released = (digitalRead(EPAPER_BUTTON_PIN) != LOW);
		if (!released && s_last_released) {
				delay(20);
				if (digitalRead(EPAPER_BUTTON_PIN) == LOW) {
						LOGI("Power", "Wake button pressed in Config/AP mode - rebooting to normal");
						epaper_show_status(g_epaper_config.epaper_rotation, []() {
								epaper_screen_returning_to_normal();
						});
						delay(100);
						ESP.restart();
				}
		}
		s_last_released = released;
}
#endif

// ---------------------------------------------------------------------------
// Duty cycle: full pipeline (was duty_cycle.cpp's `if (mode == DutyCycleEpaper)`
// block plus the splash decisions that lived in app.ino).
// ---------------------------------------------------------------------------
static bool run_duty_cycle_hook(DeviceConfig *config) {
		// Splash policy (moved from app.ino):
		//   - Cold boot: always show the boot splash so a freshly plugged-in
		//     device gets proof of life.
		//   - Button wake on a fast panel (EPAPER_FAST_REFRESH=true): show a
		//     brief "Refreshing" splash for immediate feedback.
		//   - Button wake on a slow panel: skip; the extra full refresh would
		//     dominate the time-to-new-image budget.
		//   - Timer wake: never any splash -- periodic refreshes are silent.
		const bool is_cold_boot = !power_manager_is_deep_sleep_wake();
		const bool is_button_wake = epaper_button_is_button_wake();
		const bool show_boot_splash    = is_cold_boot;
		const bool show_manual_refresh = (EPAPER_FAST_REFRESH && is_button_wake);
		if (show_boot_splash || show_manual_refresh) {
				epaper_show_status(g_epaper_config.epaper_rotation, [show_boot_splash, config]() {
						if (show_boot_splash) {
								epaper_screen_boot_splash(config->device_name, FIRMWARE_VERSION);
						} else {
								epaper_screen_manual_refresh(nullptr);
						}
				});
		}

		// Low-battery gate: power-cycle the panel briefly to read the cell
		// voltage before burning ~80 mA on WiFi for 5+ seconds. Below 3.2 V
		// the panel waveform + radio together risk a brownout reset, so paint
		// a "low battery" status frame and go back to sleep.
		if (epaper_driver_begin()) {
				const uint16_t mv = epaper_driver_battery_mv();
				if (mv > 0 && mv < 3200) {
						const uint8_t pct = epaper_battery_percent(mv);
						epaper_driver_set_rotation(g_epaper_config.epaper_rotation);
						epaper_screen_low_battery(mv, pct);
						epaper_driver_display();
						epaper_driver_sleep();
						power_manager_sleep_for(600);
						return true;
				}
				epaper_driver_sleep();
		}

		// WiFi -> CRC check -> conditional draw -> sleep. Each checkpoint feeds
		// the RTC-retained timing budget so the portal can show a per-cycle
		// breakdown.
		const bool connected = wifi_manager_connect(config, true);
		if (!connected) {
				const uint32_t backoff = power_manager_note_wifi_failure(
						config->duty_cycle_wake_seconds,
						config->wifi_backoff_max_seconds);
				epaper_timing_last.boot_to_wifi_ms = millis();
				epaper_timing_last.total_active_ms = millis();
				power_manager_sleep_for(backoff);
				return false;
		}
		power_manager_note_wifi_success();

		// Cold boot: kick SNTP so the very first image gets a real timestamp
		// instead of 1970. Subsequent wakes keep RTC across deep sleep.
		if (!power_manager_is_deep_sleep_wake()) {
				configTime(0, 0, "pool.ntp.org");
				struct tm tm_now;
				getLocalTime(&tm_now, 2000);
		}

		const uint32_t t_wifi_done = millis();
		epaper_timing_last.boot_to_wifi_ms = t_wifi_done;
		epaper_timing_last.wifi_rssi = (int16_t)WiFi.RSSI();

		// Cold boot forces a refresh: the panel currently shows only the boot
		// splash, so a CRC-match skip would leave the user staring at the
		// splash. Button wakes also force a refresh -- the user is actively
		// looking at the panel and expects fresh content.
		const bool cold_boot = !power_manager_is_deep_sleep_wake();
#if HAS_EPAPER_WAKE_BUTTON
		const bool force_refresh = cold_boot || epaper_button_is_button_wake();
#else
		const bool force_refresh = cold_boot;
#endif
		const EpaperRefreshOutcome outcome = epaper_refresh_run(config, force_refresh);
		const uint32_t t_draw_done = millis();
		epaper_timing_last.crc_retry_count = outcome.crc_retry_count;
		epaper_timing_last.crc_to_draw_ms = t_draw_done - t_wifi_done;

#if HAS_MQTT
		uint32_t t_mqtt_done = t_draw_done;
		if (strlen(config->mqtt_host) > 0) {
				const uint32_t mqtt_start = millis();
				char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
				config_manager_sanitize_device_name(config->device_name, sanitized, sizeof(sanitized));
				mqtt_manager.begin(config, config->device_name, sanitized);
				if (mqtt_manager.connectAndPublishDiscoveryBlocking(5000)) {
						epaper_mqtt_publish_state(outcome, &epaper_timing_last);
				} else {
						LOGW("Epaper", "MQTT unreachable (5s timeout); skipping telemetry");
				}
				mqtt_manager.disconnect();
				t_mqtt_done = millis();
				epaper_timing_last.draw_to_mqtt_ms = t_mqtt_done - mqtt_start;
		} else {
				epaper_timing_last.draw_to_mqtt_ms = 0;
		}
#else
		epaper_timing_last.draw_to_mqtt_ms = 0;
#endif

		// Sleep-time compensation: subtract active loop duration so wake-to-wake
		// cadence approximates duty_cycle_wake_seconds. Skip when target is 0
		// (button-only mode) so we don't accidentally re-arm the timer.
		const uint32_t target_s = config->duty_cycle_wake_seconds;
		epaper_timing_last.total_active_ms = millis();
		uint32_t sleep_s = target_s;
		if (target_s > 0) {
				const uint32_t active_s = epaper_timing_last.total_active_ms / 1000u;
				sleep_s = (active_s < target_s) ? (target_s - active_s) : 10u;
				if (sleep_s < 10u) sleep_s = 10u;
		}

#if HAS_EPAPER_FRONTLIGHT && HAS_EPAPER_WAKE_BUTTON
		// Frontlight runs only on button wakes so periodic refreshes don't
		// drain the battery lighting an empty room.
		if (g_epaper_config.epaper_frontlight_brightness > 0
		    && g_epaper_config.epaper_frontlight_duration_s > 0
		    && epaper_button_is_button_wake()) {
				LOGI("Duty", "Frontlight on (brightness=%u duration=%us)",
				     (unsigned)g_epaper_config.epaper_frontlight_brightness,
				     (unsigned)g_epaper_config.epaper_frontlight_duration_s);
				epaper_driver_begin();
				epaper_driver_frontlight_on(g_epaper_config.epaper_frontlight_brightness);
				uint32_t remaining_ms = (uint32_t)g_epaper_config.epaper_frontlight_duration_s * 1000u;
				while (remaining_ms > 0) {
						const uint32_t slice_ms = (remaining_ms > 1000u) ? 1000u : remaining_ms;
						delay(slice_ms);
						if (esp_task_wdt_status(nullptr) == ESP_OK) {
								esp_task_wdt_reset();
						}
						remaining_ms -= slice_ms;
				}
				epaper_driver_frontlight_off();
				epaper_driver_sleep();
		}
#endif

		power_manager_sleep_for(sleep_s);
		return true;
}

// ---------------------------------------------------------------------------
// MQTT discovery hook: publish e-paper-specific HA entities once per cold
// boot, and signal the core to skip the generic discovery burst on the wakes
// where we've already done it (HA retains the configs on the broker).
// ---------------------------------------------------------------------------
#if HAS_MQTT
static void mqtt_discovery_hook(MqttManager &mqtt, bool *skip_generic) {
		if (epaper_mqtt_discovery_already_published()) {
				LOGI("MQTT", "Skipping discovery (e-paper RTC flag set; retained configs persist)");
				if (skip_generic) *skip_generic = true;
				return;
		}
		// First publish in this power cycle -- emit our entities now and mark
		// the RTC flag. Generic core discovery still runs on this boot.
		epaper_mqtt_publish_ha_discovery(mqtt);
		epaper_mqtt_mark_discovery_published();
}
#endif

// ---------------------------------------------------------------------------
// Class instance + registration.
// ---------------------------------------------------------------------------
static const DeviceClass kEpaperClass = {
		/* name */              "epaper",
		/* owned_mode */        PowerMode::DutyCycleEpaper,
		/* on_setup_early */    on_setup_early_hook,
		/* on_setup_late */     on_setup_late_hook,
#if HAS_EPAPER_WAKE_BUTTON
		/* on_loop */           on_loop_hook,
#else
		/* on_loop */           nullptr,
#endif
		/* run_duty_cycle */    run_duty_cycle_hook,
		/* on_wake_classify */  wake_classify_hook,
		/* on_sleep_prepare */  sleep_prepare_hook,
		/* config_defaults */   config_defaults_hook,
		/* config_load */       config_load_hook,
		/* config_save */       config_save_hook,
		/* config_api_get */    config_api_get_hook,
		/* config_api_set */    config_api_set_hook,
#if HAS_MQTT
		/* mqtt_on_discovery */ mqtt_discovery_hook,
#else
		/* mqtt_on_discovery */ nullptr,
#endif
		/* mqtt_publish_state */ nullptr,
};

void epaper_device_class_register() {
		device_class_register(&kEpaperClass);
}

// Aggregate the rest of the e-paper translation units into this build.
// arduino-cli only compiles `.cpp` files in the sketch root; everything
// under device_classes/epaper/ is brought in via these #includes so the
// whole device class lives in one folder.
#include "epaper/epaper_crc32.cpp"
#include "epaper/epaper_drivers.cpp"
#include "epaper/epaper_mqtt.cpp"
#include "epaper/epaper_overlay.cpp"
#include "epaper/epaper_refresh.cpp"
#include "epaper/epaper_screens.cpp"
#include "epaper/epaper_timing.cpp"

#endif // HAS_EPAPER

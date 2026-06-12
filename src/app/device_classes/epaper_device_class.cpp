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
#include "epaper/epaper_carousel.h"
#include "epaper/epaper_driver.h"
#include "epaper/epaper_refresh.h"
#include "epaper/epaper_schedule.h"
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
#include <esp_sntp.h>
#include <time.h>
#if HAS_EPAPER_WAKE_BUTTON
#include <driver/rtc_io.h>  // rtc_gpio_pullup_en for deep-sleep button wake
#endif
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
static const char *kKeyRotation       = "ep_rot";
static const char *kKeyCrc32          = "ep_crc32";
static const char *kKeyCrcEnabled     = "ep_crc_en";
static const char *kKeySdCacheEn      = "ep_sd_en";
static const char *kKeyOverlayEn      = "ep_ovl_en";
static const char *kKeyOverlayPos     = "ep_ovl_pos";
static const char *kKeyOverlayCol     = "ep_ovl_col";
static const char *kKeyOverlayItems   = "ep_ovl_it";
static const char *kKeyFrontBright    = "ep_fl_b";
static const char *kKeyFrontDuration  = "ep_fl_d";
// Carousel and schedule keys (PRD B)
static const char *kKeyCarouselCount  = "ep_c_cnt";
static const char *kKeyScheduleHours  = "ep_sch_hrs";
static const char *kKeyScheduleTzOff  = "ep_sch_tz";
static const uint32_t kDefaultCarouselDurationS = 900;

bool epaper_resolve_current_url() {
		if (g_epaper_config.carousel_count == 0) {
				g_epaper_config.epaper_url[0] = '\0';
				return false;
		}

		if (g_epaper_carousel_index >= g_epaper_config.carousel_count) {
				LOGW("Epaper", "Carousel index %u >= count %u; clamping to 0", g_epaper_carousel_index, g_epaper_config.carousel_count);
				g_epaper_carousel_index = 0;
		}

		const char *slot_url = g_epaper_config.carousel[g_epaper_carousel_index].url;
		if (!slot_url || slot_url[0] == '\0') {
				g_epaper_config.epaper_url[0] = '\0';
				return false;
		}

		strlcpy(g_epaper_config.epaper_url, slot_url, CONFIG_EPAPER_URL_MAX_LEN);
		return true;
}

static uint32_t epaper_current_slot_duration_seconds() {
		if (g_epaper_config.carousel_count == 0) {
				return kDefaultCarouselDurationS;
		}
		if (g_epaper_carousel_index >= g_epaper_config.carousel_count) {
				g_epaper_carousel_index = 0;
		}
		const uint32_t duration = g_epaper_config.carousel[g_epaper_carousel_index].interval_seconds;
		return (duration > 0) ? duration : kDefaultCarouselDurationS;
}

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
		g_epaper_config.epaper_crc32_enabled = false;
		g_epaper_config.epaper_sd_cache_enabled = false;
		g_epaper_config.epaper_overlay_enabled = false;
		g_epaper_config.epaper_overlay_position = 3;
		g_epaper_config.epaper_overlay_color = 0;
		g_epaper_config.epaper_overlay_items = 0x7; // batt-icon | batt-% | time
		g_epaper_config.epaper_frontlight_brightness = 0;
		g_epaper_config.epaper_frontlight_duration_s = 30;

		// Carousel: legacy single-URL mode by default
		g_epaper_config.carousel_count = 0;
		for (int i = 0; i < 5; ++i) {
				g_epaper_config.carousel[i].url[0] = '\0';
				g_epaper_config.carousel[i].interval_seconds = 0;
				g_epaper_config.carousel[i].stay = false;
		}

		// Schedule: all hours enabled (no schedule active) by default
		g_epaper_config.schedule_hours = 0x00FFFFFF;
		g_epaper_config.schedule_tz_offset = 0;
}

static void config_load_hook(DeviceConfig * /*cfg*/, Preferences &prefs) {
		// Defaults so missing keys land on sane values.
		config_defaults_hook(nullptr);

		g_epaper_config.epaper_rotation = prefs.getUChar(kKeyRotation, 0);
		if (g_epaper_config.epaper_rotation > 3) g_epaper_config.epaper_rotation = 0;
		g_epaper_config.epaper_last_crc32 = prefs.getUInt(kKeyCrc32, 0);
		g_epaper_config.epaper_crc32_enabled = prefs.getBool(kKeyCrcEnabled, false);
		g_epaper_config.epaper_sd_cache_enabled = prefs.getBool(kKeySdCacheEn, false);

		g_epaper_config.epaper_overlay_enabled = prefs.getBool(kKeyOverlayEn, false);
		g_epaper_config.epaper_overlay_position = prefs.getUChar(kKeyOverlayPos, 3);
		if (g_epaper_config.epaper_overlay_position > 3) g_epaper_config.epaper_overlay_position = 3;
		g_epaper_config.epaper_overlay_color = prefs.getUChar(kKeyOverlayCol, 0);
		if (g_epaper_config.epaper_overlay_color > 3) g_epaper_config.epaper_overlay_color = 0;
		g_epaper_config.epaper_overlay_items = prefs.getUChar(kKeyOverlayItems, 0x7);

		g_epaper_config.epaper_frontlight_brightness = prefs.getUChar(kKeyFrontBright, 0);
		if (g_epaper_config.epaper_frontlight_brightness > 63) g_epaper_config.epaper_frontlight_brightness = 63;
		g_epaper_config.epaper_frontlight_duration_s = prefs.getUShort(kKeyFrontDuration, 30);

		// Load carousel
		g_epaper_config.carousel_count = prefs.getUChar(kKeyCarouselCount, 0);
		if (g_epaper_config.carousel_count > 5) g_epaper_config.carousel_count = 5;
		for (int i = 0; i < 5; ++i) {
				char key_url[16], key_int[16], key_stay[16];
				snprintf(key_url, sizeof(key_url), "ep_c%d_url", i);
				snprintf(key_int, sizeof(key_int), "ep_c%d_int", i);
				snprintf(key_stay, sizeof(key_stay), "ep_c%d_stay", i);
				String carousel_url = prefs.getString(key_url, "");
				strlcpy(g_epaper_config.carousel[i].url, carousel_url.c_str(), CONFIG_EPAPER_URL_MAX_LEN);
				g_epaper_config.carousel[i].interval_seconds = prefs.getUInt(key_int, 0);
				g_epaper_config.carousel[i].stay = prefs.getBool(key_stay, false);
		}

		// Load schedule
		g_epaper_config.schedule_hours = prefs.getUInt(kKeyScheduleHours, 0x00FFFFFF);
		g_epaper_config.schedule_tz_offset = prefs.getChar(kKeyScheduleTzOff, 0);
		if (g_epaper_config.schedule_tz_offset < -12) g_epaper_config.schedule_tz_offset = -12;
		if (g_epaper_config.schedule_tz_offset > 14) g_epaper_config.schedule_tz_offset = 14;
}

static void config_save_hook(const DeviceConfig * /*cfg*/, Preferences &prefs) {
		prefs.putUChar(kKeyRotation, g_epaper_config.epaper_rotation);
		prefs.putUInt(kKeyCrc32, g_epaper_config.epaper_last_crc32);
		prefs.putBool(kKeyCrcEnabled, g_epaper_config.epaper_crc32_enabled);
		prefs.putBool(kKeySdCacheEn, g_epaper_config.epaper_sd_cache_enabled);
		prefs.putBool(kKeyOverlayEn, g_epaper_config.epaper_overlay_enabled);
		prefs.putUChar(kKeyOverlayPos, g_epaper_config.epaper_overlay_position);
		prefs.putUChar(kKeyOverlayCol, g_epaper_config.epaper_overlay_color);
		prefs.putUChar(kKeyOverlayItems, g_epaper_config.epaper_overlay_items);
		prefs.putUChar(kKeyFrontBright, g_epaper_config.epaper_frontlight_brightness);
		prefs.putUShort(kKeyFrontDuration, g_epaper_config.epaper_frontlight_duration_s);

		// Save carousel (only write non-empty entries to save NVS space)
		prefs.putUChar(kKeyCarouselCount, g_epaper_config.carousel_count);
		for (int i = 0; i < 5; ++i) {
				char key_url[16], key_int[16], key_stay[16];
				snprintf(key_url, sizeof(key_url), "ep_c%d_url", i);
				snprintf(key_int, sizeof(key_int), "ep_c%d_int", i);
				snprintf(key_stay, sizeof(key_stay), "ep_c%d_stay", i);
				// Only write non-empty carousel entries
				if (g_epaper_config.carousel[i].url[0] != '\0') {
						prefs.putString(key_url, g_epaper_config.carousel[i].url);
						prefs.putUInt(key_int, g_epaper_config.carousel[i].interval_seconds);
						prefs.putBool(key_stay, g_epaper_config.carousel[i].stay);
				}
		}

		// Save schedule
		prefs.putUInt(kKeyScheduleHours, g_epaper_config.schedule_hours);
		prefs.putChar(kKeyScheduleTzOff, g_epaper_config.schedule_tz_offset);
}

static void config_api_get_hook(const DeviceConfig * /*cfg*/, JsonObject &root) {
		root["caps"]["epaper"] = true;
		root["epaper_rotation"] = g_epaper_config.epaper_rotation;
		root["epaper_crc32_enabled"] = g_epaper_config.epaper_crc32_enabled;
		root["epaper_sd_cache_enabled"] = g_epaper_config.epaper_sd_cache_enabled;
		root["epaper_sd_cache_supported"] = (bool)
#ifdef EPAPER_SD_CS_PIN
			true
#else
			false
#endif
			;
		root["epaper_overlay_enabled"] = g_epaper_config.epaper_overlay_enabled;
		root["epaper_overlay_position"] = g_epaper_config.epaper_overlay_position;
		root["epaper_overlay_color"] = g_epaper_config.epaper_overlay_color;
		root["epaper_overlay_items"] = g_epaper_config.epaper_overlay_items;
		root["epaper_frontlight_brightness"] = g_epaper_config.epaper_frontlight_brightness;
		root["epaper_frontlight_duration_s"] = g_epaper_config.epaper_frontlight_duration_s;
		root["epaper_frontlight_supported"] = (bool)HAS_EPAPER_FRONTLIGHT;

		// Carousel
		root["epaper_carousel_count"] = g_epaper_config.carousel_count;
		JsonArray carousel_array = root.createNestedArray("epaper_carousel");
		for (int i = 0; i < g_epaper_config.carousel_count; ++i) {
				JsonObject entry = carousel_array.createNestedObject();
				entry["url"] = g_epaper_config.carousel[i].url;
				entry["interval_seconds"] = g_epaper_config.carousel[i].interval_seconds;
				entry["stay"] = g_epaper_config.carousel[i].stay;
		}

		// Schedule
		root["epaper_schedule_hours"] = g_epaper_config.schedule_hours;
		root["epaper_schedule_tz_offset"] = g_epaper_config.schedule_tz_offset;
}

static void config_api_set_hook(DeviceConfig * /*cfg*/, JsonObject &body) {
		if (body.containsKey("epaper_rotation")) {
				uint8_t v = body["epaper_rotation"].is<const char*>()
						? (uint8_t)atoi(body["epaper_rotation"].as<const char*>())
						: (uint8_t)(body["epaper_rotation"] | 0);
				if (v > 3) v = 0;
				g_epaper_config.epaper_rotation = v;
		}		if (body.containsKey("epaper_crc32_enabled")) {
			g_epaper_config.epaper_crc32_enabled = body["epaper_crc32_enabled"] | false;
		}		if (body.containsKey("epaper_sd_cache_enabled")) {
			g_epaper_config.epaper_sd_cache_enabled = body["epaper_sd_cache_enabled"] | false;
		}		if (body.containsKey("epaper_overlay_enabled")) {
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

		// Parse carousel
		if (body.containsKey("epaper_carousel")) {
				JsonArray carousel_array = body["epaper_carousel"];
				uint8_t count = 0;
				for (int i = 0; i < 5; ++i) {
						if (i < (int)carousel_array.size() && carousel_array[i].is<JsonObject>()) {
								JsonObject entry = carousel_array[i].as<JsonObject>();
								const char *url = entry["url"] | "";
								if (url[0] != '\0') {
										strlcpy(g_epaper_config.carousel[i].url, url, CONFIG_EPAPER_URL_MAX_LEN);
										uint32_t interval = entry["interval_seconds"] | 0;
										if (interval == 0) interval = kDefaultCarouselDurationS;
										g_epaper_config.carousel[i].interval_seconds = interval;
										g_epaper_config.carousel[i].stay = entry["stay"] | false;
										count = (uint8_t)(i + 1);
								} else {
										g_epaper_config.carousel[i].url[0] = '\0';
										g_epaper_config.carousel[i].interval_seconds = 0;
										g_epaper_config.carousel[i].stay = false;
								}
						} else {
								g_epaper_config.carousel[i].url[0] = '\0';
								g_epaper_config.carousel[i].interval_seconds = 0;
								g_epaper_config.carousel[i].stay = false;
						}
				}
				g_epaper_config.carousel_count = count;
		}

		// Parse schedule
		if (body.containsKey("epaper_schedule_hours")) {
				uint32_t v = body["epaper_schedule_hours"].is<const char*>()
						? (uint32_t)strtoul(body["epaper_schedule_hours"].as<const char*>(), nullptr, 10)
						: (uint32_t)(body["epaper_schedule_hours"] | 0xFFFFFF);
				g_epaper_config.schedule_hours = v & 0xFFFFFF;
		}
		if (body.containsKey("epaper_schedule_tz_offset")) {
				int8_t v = body["epaper_schedule_tz_offset"].is<const char*>()
						? (int8_t)atoi(body["epaper_schedule_tz_offset"].as<const char*>())
						: (int8_t)(body["epaper_schedule_tz_offset"] | 0);
				if (v < -12) v = -12;
				if (v > 14) v = 14;
				g_epaper_config.schedule_tz_offset = v;
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
		// wake sources (ext1 here). The core clamps 0->1 only when no class owns
		// the active power mode.
		(void)seconds_inout;
#if HAS_EPAPER_WAKE_BUTTON
		// Match the Seeed reTerminal LowPower_DeepSleep reference: use ext1 (more
		// robust than ext0 on the ESP32-S3) and enable the RTC-domain pull-up so
		// the active-low button line stays HIGH while asleep. The board's HW
		// pull-up is disabled in the RTC domain during deep sleep, so without
		// rtc_gpio_pullup_en() the line can drift and the falling edge is never
		// latched -- the press appears to do nothing.
		const gpio_num_t wake_pin = (gpio_num_t)EPAPER_BUTTON_PIN;
		rtc_gpio_pullup_en(wake_pin);
		rtc_gpio_pulldown_dis(wake_pin);
		// The original ESP32 (Inkplate) only defines ESP_EXT1_WAKEUP_ALL_LOW; the
		// ANY_LOW logic mode is S3/C-series only. For a single-GPIO wake mask the
		// two are semantically identical (one selected pin going low satisfies both
		// "all" and "any"), so pick whichever the target SoC's enum exposes.
#if CONFIG_IDF_TARGET_ESP32
		esp_sleep_enable_ext1_wakeup(1ULL << EPAPER_BUTTON_PIN, ESP_EXT1_WAKEUP_ALL_LOW);
#else
		esp_sleep_enable_ext1_wakeup(1ULL << EPAPER_BUTTON_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
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

// Set by the SNTP service (from the lwIP task) when a fresh time response is
// applied. Polled by the resync below so we can block until an actual sync
// lands instead of racing the image download on the WiFi stack.
static volatile bool s_epaper_ntp_synced = false;
static void epaper_ntp_sync_cb(struct timeval * /*tv*/) {
		s_epaper_ntp_synced = true;
}

// UTC epoch of the last successful NTP fetch, retained across deep sleep so we
// can throttle real resyncs to once per interval instead of every wake.
RTC_DATA_ATTR static time_t s_epaper_last_ntp_epoch = 0;

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

		// Low-battery gate: read the cell voltage before burning ~80 mA on WiFi
		// for 5+ seconds. Below 3.2 V the panel waveform + radio together risk a
		// brownout reset, so paint a "low battery" status frame and go back to
		// sleep. Boards whose battery sense is independent of the panel (E1003)
		// read the cell up front and then overlap the slow panel init with the
		// WiFi connect; boards whose sense is gated behind begin() (Inkplate)
		// must power the panel up first.

		auto low_battery_sleep = [&](uint16_t mv) {
				const uint8_t pct = epaper_battery_percent(mv);
				epaper_driver_set_rotation(g_epaper_config.epaper_rotation);
				epaper_screen_low_battery(mv, pct);
				epaper_driver_display();
				epaper_driver_sleep();
				power_manager_sleep_for(600);
		};

		bool begin_started = false;  // true once begin_async() has been kicked off
		if (epaper_driver_battery_ready_before_begin()) {
				const uint16_t mv = epaper_driver_battery_mv();
				LOGI("Epaper", "Battery %u mV (early read)", mv);
				if (mv > 0 && mv < 3200) {
						epaper_driver_begin();  // bring panel up to paint the status frame
						low_battery_sleep(mv);
						return true;
				}
				// Healthy: start panel init on a background task so it overlaps the
				// WiFi connect below. Joined before the first draw.
				epaper_driver_begin_async();
				begin_started = true;
		} else if (epaper_driver_begin()) {
				const uint16_t mv = epaper_driver_battery_mv();
				LOGI("Epaper", "Battery %u mV", mv);
				if (mv > 0 && mv < 3200) {
						low_battery_sleep(mv);
						return true;
				}
				// Healthy: panel already up; leave it powered through the WiFi connect
				// so the draw-path power_on() collapses to a guarded no-op (the slow
				// ~1.6 s IT8951 power-on is paid once, here, not again in the draw).
		} else {
				LOGW("Epaper", "driver begin returned false");
		}

		// WiFi -> CRC check -> conditional draw -> sleep. Each checkpoint feeds
		// the RTC-retained timing budget so the portal can show a per-cycle
		// breakdown. On early-battery boards the background panel init started
		// above runs concurrently with this association.
		const bool connected = wifi_manager_connect(config, true);

		// Ensure panel init has finished (and reap its task) before any later
		// draw. No-op on boards where begin() ran synchronously above.
		if (begin_started && !epaper_driver_begin_join()) {
				LOGW("Epaper", "driver begin returned false");
		}

		if (!connected) {
				const uint32_t backoff = power_manager_note_wifi_failure(
						epaper_current_slot_duration_seconds(),
						config->wifi_backoff_max_seconds);
				epaper_timing_last.boot_to_wifi_ms = millis();
				epaper_timing_last.total_active_ms = millis();
				power_manager_sleep_for(backoff);
				return false;
		}
		power_manager_note_wifi_success();

		// NTP policy: the RTC clock survives deep sleep, so on a normal wake the
		// time is already valid and we skip the network sync entirely -- the
		// common path costs ~0 ms and starts no SNTP traffic that could collide
		// with the time-critical HTTPS image download. We only pay for a real
		// fetch when the clock is stale (cold boot) or the last successful sync
		// is older than the resync interval, matching ESP-IDF's default 1 h SNTP
		// update cadence. This bounds RTC drift (the internal RC oscillator
		// drifts seconds per day) without taxing every wake.
		//
		// When a fetch is due we block until an actual SNTP response lands (or
		// the ceiling elapses) BEFORE starting the download. A fire-and-forget
		// configTime() leaves the SNTP service doing DNS lookups + UDP traffic
		// concurrently with the download, which inflated crc_to_draw_ms by ~10s
		// on some wakes. Waiting synchronously keeps the download contention-free
		// and makes ntp_sync_ms reflect the true cost. Fail-open: if no response
		// arrives within the ceiling, proceed with the current clock.
		{
				static const uint32_t kEpaperNtpMaxWaitMs    = 5000;  // ceiling for an unreachable server
				static const time_t   kEpaperNtpResyncIntervalS = 3600;  // 1 h, matches ESP-IDF SNTP default

				const time_t now = time(nullptr);
				const bool clock_valid = (now >= (time_t)EPAPER_SCHEDULE_MIN_VALID_EPOCH);
				const bool due_for_resync =
						!clock_valid ||
						s_epaper_last_ntp_epoch == 0 ||
						(now - s_epaper_last_ntp_epoch) >= kEpaperNtpResyncIntervalS;

				if (!due_for_resync) {
						epaper_timing_last.ntp_sync_ms = 0;
						LOGI("Epaper", "NTP resync skipped (last sync %lds ago)",
								 (long)(now - s_epaper_last_ntp_epoch));
				} else {
						const uint32_t ntp_start = millis();
						s_epaper_ntp_synced = false;
						sntp_set_time_sync_notification_cb(epaper_ntp_sync_cb);
						configTime(0, 0, "pool.ntp.org", "time.nist.gov");
						for (uint32_t waited = 0; waited < kEpaperNtpMaxWaitMs && !s_epaper_ntp_synced; waited += 50) {
								delay(50);
						}
						epaper_timing_last.ntp_sync_ms = millis() - ntp_start;
						if (s_epaper_ntp_synced) {
								s_epaper_last_ntp_epoch = time(nullptr);
								LOGI("Epaper", "NTP resync done in %ums", epaper_timing_last.ntp_sync_ms);
						} else {
								LOGW("Epaper", "NTP sync incomplete after %ums; proceeding with current clock", epaper_timing_last.ntp_sync_ms);
						}
				}
		}

		const uint32_t t_wifi_done = millis();
		epaper_timing_last.boot_to_wifi_ms = t_wifi_done;
		epaper_timing_last.wifi_rssi = (int16_t)WiFi.RSSI();

		// A WAKE-button press always wins over the schedule: the user is
		// actively looking at the panel and expects fresh content, so a button
		// wake bypasses the schedule gate below and forces a refresh even
		// outside the enabled hours.
		const bool cold_boot = !power_manager_is_deep_sleep_wake();
#if HAS_EPAPER_WAKE_BUTTON
		const bool button_wake = epaper_button_is_button_wake();
#else
		const bool button_wake = false;
#endif

		// Schedule check BEFORE image fetch: if disabled at this hour, sleep and
		// skip refresh. Skipped on a button wake so the button always refreshes.
		if (g_epaper_config.schedule_hours != 0x00FFFFFF && !button_wake) {
				if (!epaper_schedule_should_refresh(g_epaper_config.schedule_hours, g_epaper_config.schedule_tz_offset, time(nullptr))) {
						uint32_t sleep_s = epaper_schedule_seconds_to_next(g_epaper_config.schedule_hours, g_epaper_config.schedule_tz_offset, time(nullptr));
						LOGI("Epaper", "Schedule: disabled at this hour; sleeping %u seconds", sleep_s);
						power_manager_sleep_for(sleep_s);
						return true;
				}
		}

		// Cold boot forces a refresh: the panel currently shows only the boot
		// splash, so a CRC-match skip would leave the user staring at the
		// splash. Button wakes also force a refresh -- the user is actively
		// looking at the panel and expects fresh content.
#if HAS_EPAPER_WAKE_BUTTON
		const bool force_refresh = cold_boot || button_wake;
#else
		const bool force_refresh = cold_boot;
#endif


		if (!epaper_resolve_current_url()) {
				LOGW("Epaper", "Refresh skipped: no carousel URL configured");
				power_manager_sleep_for(kDefaultCarouselDurationS);
				return true;
		}
		const uint8_t active_slot_index = g_epaper_carousel_index;
		LOGI("Epaper", "Carousel: using slot %u URL: %s", g_epaper_carousel_index, g_epaper_config.epaper_url);

		const EpaperRefreshOutcome outcome = epaper_refresh_run(config, force_refresh);
		const uint32_t t_draw_done = millis();
		epaper_timing_last.crc_retry_count = outcome.crc_retry_count;

		// Carousel: advance index after refresh (on success or skip)
		if (g_epaper_config.carousel_count > 0) {
				uint8_t next_idx = epaper_carousel_next_index(
						g_epaper_carousel_index,
						g_epaper_config.carousel_count,
						g_epaper_config.carousel[g_epaper_carousel_index].stay);
				g_epaper_carousel_index = next_idx;
				LOGI("Epaper", "Carousel: advanced to slot %u", next_idx);
		}
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
		// Per-entry interval: if carousel active, use current entry's interval (if > 0)
		uint32_t target_s = kDefaultCarouselDurationS;
		if (g_epaper_config.carousel_count > 0) {
				target_s = g_epaper_config.carousel[active_slot_index].interval_seconds;
				if (target_s == 0) target_s = kDefaultCarouselDurationS;
				LOGI("Epaper", "Using carousel slot %u duration: %u seconds", active_slot_index, target_s);
		}

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
#include "epaper/epaper_carousel.cpp"
#include "epaper/epaper_http.cpp"
#include "epaper/epaper_drivers.cpp"
#include "epaper/epaper_mqtt.cpp"
#include "epaper/epaper_overlay.cpp"
#include "epaper/epaper_refresh.cpp"
#include "epaper/epaper_schedule.cpp"
#include "epaper/epaper_screens.cpp"
#include "epaper/epaper_sd_cache.cpp"
#include "epaper/epaper_timing.cpp"

#endif // HAS_EPAPER

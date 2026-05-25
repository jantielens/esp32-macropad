#include "duty_cycle.h"

#include "ble_telemetry.h"
#include "config_manager.h"
#include "device_telemetry.h"
#include "log_manager.h"
#include "mqtt_manager.h"
#include "power_config.h"
#include "power_manager.h"
#include "sensors/sensor_manager.h"
#include "wifi_manager.h"

#if HAS_EPAPER
#include "epaper_driver.h"
#include "epaper_battery.h"
#include "epaper_refresh.h"
#include "epaper_screens.h"
#include "epaper_timing.h"
#if HAS_MQTT
#include "epaper_mqtt.h"
#endif
#endif

#include <ArduinoJson.h>
#if HAS_EPAPER
#include <WiFi.h>
#include <time.h>
#if defined(ARDUINO_ARCH_ESP32) && HAS_EPAPER_FRONTLIGHT
#include <esp_task_wdt.h>
#endif
#endif

#if HAS_MQTT
extern MqttManager mqtt_manager;
#endif

static void build_sensor_json(JsonDocument &doc) {
		JsonObject root = doc.to<JsonObject>();
		sensor_manager_append_mqtt(root);
}

bool duty_cycle_run(DeviceConfig *config) {
		if (!config) return false;

		const PowerMode mode = power_manager_get_current_mode();
		LOGI("Duty", "Start (mode=%s)", power_config_power_mode_to_string(mode));

		// Collect sensor data. As a side effect this triggers each sensor's
		// update path, which is where BLE telemetry values are buffered for
		// the upcoming advertisement.
		StaticJsonDocument<512> sensors_doc;
		build_sensor_json(sensors_doc);

#if HAS_EPAPER
		if (mode == PowerMode::DutyCycleEpaper) {
				// Low-battery gate: power-cycle the panel briefly to read the cell
				// voltage before we burn ~80 mA on WiFi for 5+ seconds. If we're
				// below 3.2 V the panel waveform + radio together risk a brownout
				// reset, so paint a "low battery" status frame and go back to sleep.
				if (epaper_driver_begin()) {
						const uint16_t mv = epaper_driver_battery_mv();
						if (mv > 0 && mv < 3200) {
								const uint8_t pct = epaper_battery_percent(mv);
								epaper_driver_set_rotation(config->epaper_rotation);
								epaper_screen_low_battery(mv, pct);
								epaper_driver_display();
								epaper_driver_sleep();
								// Sleep long (10 minutes) so the user has time to plug in
								// before we keep retrying. Don't churn the panel.
								power_manager_sleep_for(600);
								return true;
						}
						epaper_driver_sleep();
				}

				// E-paper duty cycle: WiFi -> CRC check -> conditional draw -> sleep.
				// Each checkpoint feeds the RTC-retained timing budget so the portal
				// and (optionally) MQTT can show a per-cycle breakdown.
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

				// Cold boot: ESP32 RTC has no wall clock yet, so the overlay would
				// print a 1970 timestamp on the first refresh. Kick SNTP and wait
				// briefly so the very first image gets a real time. Subsequent
				// wakes keep RTC across deep sleep, so we only pay this cost on
				// the first wake after a power cycle.
				if (!power_manager_is_deep_sleep_wake()) {
						configTime(0, 0, "pool.ntp.org");
						struct tm tm_now;
						getLocalTime(&tm_now, 2000);
				}

				const uint32_t t_wifi_done = millis();
				epaper_timing_last.boot_to_wifi_ms = t_wifi_done;
				epaper_timing_last.wifi_rssi = (int16_t)WiFi.RSSI();

				// Cold boot also forces an image refresh: the panel currently
				// shows only the boot splash, so skipping on a CRC match would
				// leave the user looking at the splash until the dashboard image
				// next changes. Without this, a fresh power-up with an unchanged
				// sidecar CRC never shows the image.
				const bool cold_boot = !power_manager_is_deep_sleep_wake();
#if HAS_EPAPER_WAKE_BUTTON
				// Any button wake forces a refresh: the user just saw the boot
				// splash and is actively looking at the panel, so always give them
				// fresh content even if the sidecar CRC matches the cached value.
				const bool force_refresh = cold_boot || power_manager_is_button_wake();
#else
				const bool force_refresh = cold_boot;
#endif
				const EpaperRefreshOutcome outcome = epaper_refresh_run(config, force_refresh);
				const uint32_t t_draw_done = millis();
				// The refresh call internally fetches the sidecar then draws; we don't
				// have separate checkpoints inside, so attribute crc-fetch time to
				// the sidecar's retry count and the rest to crc_to_draw_ms.
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
								epaper_mqtt_publish_state(config, outcome, &epaper_timing_last);
						} else {
								LOGW("Epaper", "MQTT unreachable (5s timeout); skipping telemetry");
						}
						// Always send a clean DISCONNECT to avoid spurious LWT on the broker.
						mqtt_manager.disconnect();
						t_mqtt_done = millis();
						epaper_timing_last.draw_to_mqtt_ms = t_mqtt_done - mqtt_start;
				} else {
						epaper_timing_last.draw_to_mqtt_ms = 0;
				}
#else
				epaper_timing_last.draw_to_mqtt_ms = 0;
#endif

				// Sleep-time compensation: subtract the active loop duration so the
				// wake-to-wake cadence approximates `duty_cycle_wake_seconds`. Skip
				// entirely when target is 0 (button-only mode) so we don't accidentally
				// re-arm the timer.
				const uint32_t target_s = config->duty_cycle_wake_seconds;
				epaper_timing_last.total_active_ms = millis();
				uint32_t sleep_s = target_s;
				if (target_s > 0) {
						const uint32_t active_s = epaper_timing_last.total_active_ms / 1000u;
						sleep_s = (active_s < target_s) ? (target_s - active_s) : 10u;
						if (sleep_s < 10u) sleep_s = 10u;  // floor short remainders too
				}

#if HAS_EPAPER_FRONTLIGHT && HAS_EPAPER_WAKE_BUTTON
				// Frontlight runs only on button wakes so periodic refreshes don't
				// drain the battery lighting an empty room.
				if (config->epaper_frontlight_brightness > 0
				    && config->epaper_frontlight_duration_s > 0
				    && power_manager_is_button_wake()) {
						LOGI("Duty", "Frontlight on (brightness=%u duration=%us)",
							 (unsigned)config->epaper_frontlight_brightness,
							 (unsigned)config->epaper_frontlight_duration_s);
						epaper_driver_begin();
						epaper_driver_frontlight_on(config->epaper_frontlight_brightness);
						uint32_t remaining_ms = (uint32_t)config->epaper_frontlight_duration_s * 1000u;
						while (remaining_ms > 0) {
								const uint32_t slice_ms = (remaining_ms > 1000u) ? 1000u : remaining_ms;
								delay(slice_ms);
#if defined(ARDUINO_ARCH_ESP32)
								if (esp_task_wdt_status(nullptr) == ESP_OK) {
										esp_task_wdt_reset();
								}
#endif
								remaining_ms -= slice_ms;
						}
						epaper_driver_frontlight_off();
						epaper_driver_sleep();
				}
#endif

				power_manager_sleep_for(sleep_s);
				return true;
		}
#endif

#if HAS_BLE
		if (mode == PowerMode::DutyCycleBle) {
				const uint8_t burst = config->ble_burst_count > 0
						? config->ble_burst_count
						: BLE_TELEMETRY_DEFAULT_BURST_COUNT;
				const uint16_t interval = config->ble_adv_interval_ms > 0
						? config->ble_adv_interval_ms
						: BLE_TELEMETRY_DEFAULT_ADV_INTERVAL_MS;
				if (!ble_telemetry_advertise_burst(burst, interval)) {
						LOGW("Duty", "BLE telemetry burst returned false (no data?)");
				}
				power_manager_sleep_for(config->duty_cycle_wake_seconds);
				return true;
		}
#endif

		if (strlen(config->mqtt_host) == 0) {
				LOGW("MQTT", "MQTT transport requested but mqtt_host is empty");
		} else {
				const bool connected = wifi_manager_connect(config, true);
				if (!connected) {
						const uint32_t backoff = power_manager_note_wifi_failure(config->duty_cycle_wake_seconds, config->wifi_backoff_max_seconds);
						power_manager_sleep_for(backoff);
						return false;
				}

				power_manager_note_wifi_success();

				#if HAS_MQTT
				char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
				config_manager_sanitize_device_name(config->device_name, sanitized, sizeof(sanitized));
				mqtt_manager.begin(config, config->device_name, sanitized);

				const unsigned long start = millis();
				while (millis() - start < 5000) {
						mqtt_manager.loop();
						if (mqtt_manager.connected()) break;
						delay(50);
				}

				mqtt_manager.disconnect();
				#else
				LOGE("MQTT", "MQTT transport requested but HAS_MQTT=false");
				#endif
		}

		power_manager_sleep_for(config->duty_cycle_wake_seconds);
		return true;
}

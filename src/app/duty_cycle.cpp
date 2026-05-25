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
#include "epaper_refresh.h"
#endif

#include <ArduinoJson.h>

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
				// E-paper duty cycle: WiFi -> CRC check -> conditional draw -> sleep.
				const bool connected = wifi_manager_connect(config, true);
				if (!connected) {
						const uint32_t backoff = power_manager_note_wifi_failure(
								config->duty_cycle_wake_seconds,
								config->wifi_backoff_max_seconds);
						power_manager_sleep_for(backoff);
						return false;
				}
				power_manager_note_wifi_success();

#if HAS_EPAPER_WAKE_BUTTON
				const bool force_refresh =
						power_manager_get_button_wake_action() == EpaperButtonWakeAction::Refresh;
#else
				const bool force_refresh = false;
#endif
				epaper_refresh_run(config, force_refresh);

				power_manager_sleep_for(config->duty_cycle_wake_seconds);
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

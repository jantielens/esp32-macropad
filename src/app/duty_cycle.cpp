#include "duty_cycle.h"

#include "ble_advertiser.h"
#include "config_manager.h"
#include "device_telemetry.h"
#include "log_manager.h"
#include "mqtt_manager.h"
#include "power_config.h"
#include "power_manager.h"
#include "sensors/sensor_manager.h"
#include "wifi_manager.h"

#include <ArduinoJson.h>

#if HAS_MQTT
extern MqttManager mqtt_manager;
#endif

static void build_sensor_json(JsonDocument &doc) {
		JsonObject root = doc.to<JsonObject>();
		sensor_manager_append_mqtt(root);
}

bool duty_cycle_run(const DeviceConfig *config) {
		if (!config) return false;

		const PublishTransport transport = power_config_parse_publish_transport(config);
		const bool want_ble = power_config_transport_includes_ble(transport);
		const bool want_mqtt = power_config_transport_includes_mqtt(transport);

		LOGI("Duty", "Start (ble=%s mqtt=%s)", want_ble ? "true" : "false", want_mqtt ? "true" : "false");

		// Collect sensor data once for BLE and MQTT.
		StaticJsonDocument<512> sensors_doc;
		build_sensor_json(sensors_doc);

		if (want_ble) {
				#if HAS_BLE
				if (!ble_advertiser_advertise_bthome(config, sensors_doc.as<JsonObject>(), true)) {
						LOGE("BLE", "Advertise failed");
				}
				#else
				LOGE("BLE", "BLE transport requested but HAS_BLE=false");
				#endif
		}

		if (want_mqtt) {
				if (strlen(config->mqtt_host) == 0) {
						LOGW("MQTT", "MQTT transport requested but mqtt_host is empty");
				} else {
						const bool connected = wifi_manager_connect(config, true);
						if (!connected) {
								const uint32_t backoff = power_manager_note_wifi_failure(config->cycle_interval_seconds, config->wifi_backoff_max_seconds);
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
		}

		power_manager_sleep_for(config->cycle_interval_seconds);
		return true;
}

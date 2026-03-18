#include "ha_discovery.h"

#include "board_config.h"

#if HAS_MQTT

#include "mqtt_manager.h"
#include "display_manager.h"
#include "sensors/sensor_manager.h"
#include "web_assets.h" // PROJECT_DISPLAY_NAME
#include "../version.h" // FIRMWARE_VERSION
#include <ArduinoJson.h>

// Forward declaration — used by all entity publish helpers.
static void fill_device_block(JsonDocument &doc, MqttManager &mqtt);

void ha_discovery_publish_health(MqttManager &mqtt) {
		// Notes:
		// - Single JSON publish model: all entities share the same stat_t.
		// - value_template extracts fields from the JSON payload.

		ha_discovery_publish_sensor_config(mqtt, "uptime", "Uptime", "{{ value_json.uptime_seconds }}", "s", "duration", "measurement", "diagnostic");
		ha_discovery_publish_sensor_config(mqtt, "reset_reason", "Reset Reason", "{{ value_json.reset_reason }}", "", "", "", "diagnostic");

		ha_discovery_publish_sensor_config(mqtt, "cpu_usage", "CPU Usage", "{{ value_json.cpu_usage }}", "%", "", "measurement", "diagnostic");
		ha_discovery_publish_sensor_config(mqtt, "cpu_temperature", "Core Temp", "{{ value_json.cpu_temperature }}", "°C", "temperature", "measurement", "diagnostic");

		ha_discovery_publish_sensor_config(mqtt, "heap_fragmentation", "Heap Fragmentation", "{{ value_json.heap_fragmentation }}", "%", "", "measurement", "diagnostic");

		ha_discovery_publish_sensor_config(mqtt, "heap_internal_free", "Internal Heap Free", "{{ value_json.heap_internal_free }}", "B", "", "measurement", "diagnostic");
		ha_discovery_publish_sensor_config(mqtt, "heap_internal_min", "Internal Heap Min", "{{ value_json.heap_internal_min }}", "B", "", "measurement", "diagnostic");
		ha_discovery_publish_sensor_config(mqtt, "heap_internal_largest", "Internal Heap Largest", "{{ value_json.heap_internal_largest }}", "B", "", "measurement", "diagnostic");

		ha_discovery_publish_sensor_config(mqtt, "psram_free", "PSRAM Free", "{{ value_json.psram_free }}", "B", "", "measurement", "diagnostic");
		ha_discovery_publish_sensor_config(mqtt, "psram_min", "PSRAM Min Free", "{{ value_json.psram_min }}", "B", "", "measurement", "diagnostic");

		ha_discovery_publish_sensor_config(mqtt, "flash_used", "Flash Used", "{{ value_json.flash_used }}", "B", "", "measurement", "diagnostic");
		ha_discovery_publish_sensor_config(mqtt, "flash_total", "Flash Total", "{{ value_json.flash_total }}", "B", "", "measurement", "diagnostic");

		ha_discovery_publish_binary_sensor_config(mqtt, "fs_mounted", "FS Mounted", "{{ 'ON' if value_json.fs_mounted else 'OFF' }}", "", "diagnostic");
		ha_discovery_publish_sensor_config(mqtt, "fs_used_bytes", "FS Used", "{{ value_json.fs_used_bytes }}", "B", "", "measurement", "diagnostic");
		ha_discovery_publish_sensor_config(mqtt, "fs_total_bytes", "FS Total", "{{ value_json.fs_total_bytes }}", "B", "", "measurement", "diagnostic");

		#if HAS_DISPLAY
		// display_fps is API-only (not in MQTT payload), no HA entity needed.
		#endif

		ha_discovery_publish_sensor_config(mqtt, "wifi_rssi", "WiFi RSSI", "{{ value_json.wifi_rssi }}", "dBm", "signal_strength", "measurement", "diagnostic");

		// =====================================================================
		// USER-EXTEND: Add your own Home Assistant entities here
		// =====================================================================
		// To add new sensors (e.g. ambient temperature + humidity), you typically:
		//   1) Add JSON fields to device_telemetry_fill_mqtt() in device_telemetry.cpp
		//   2) Add matching discovery entries below (value_template must match keys)
		//
		// Example (commented out): External temperature/humidity
		// (These will show up under the normal Sensors category in Home Assistant.)
		// ha_discovery_publish_sensor_config(mqtt, "temperature", "Temperature", "{{ value_json.temperature }}", "°C", "temperature", "measurement", nullptr);
		// ha_discovery_publish_sensor_config(mqtt, "humidity", "Humidity", "{{ value_json.humidity }}", "%", "humidity", "measurement", nullptr);

		// Sensor adapters self-register their discovery entries.
		sensor_manager_publish_ha_discovery(mqtt);

		// Pad button press event entity
		#if HAS_DISPLAY
		ha_discovery_publish_button_event_config(mqtt);
		ha_discovery_publish_screen_select_config(mqtt);
		#endif

		// Audio entities (siren, volume, beep buttons)
		#if HAS_AUDIO
		ha_discovery_publish_audio_entities(mqtt);
		#endif
}

bool ha_discovery_publish_binary_sensor_config(
		MqttManager &mqtt,
		const char *object_id,
		const char *name_suffix,
		const char *value_template,
		const char *device_class,
		const char *entity_category,
		const char *state_topic
) {
		char topic[160];
		snprintf(topic, sizeof(topic), "homeassistant/binary_sensor/%s/%s/config", mqtt.sanitizedName(), object_id);

		StaticJsonDocument<768> doc;

		// Use base topic shortcut to keep discovery payload small
		doc["~"] = mqtt.baseTopic();

		// Friendly name in payload
		// Keep entity name short; HA already groups entities under the device name.
		doc["name"] = name_suffix;

		// Provide a stable object_id that includes the device name once.
		char ha_object_id[96];
		snprintf(ha_object_id, sizeof(ha_object_id), "%s_%s", mqtt.sanitizedName(), object_id);
		doc["object_id"] = ha_object_id;

		if (entity_category && strlen(entity_category) > 0) {
				doc["entity_category"] = entity_category;
		}

		// Stable unique id (sanitized name + object id)
		char uniq_id[96];
		snprintf(uniq_id, sizeof(uniq_id), "%s_%s", mqtt.sanitizedName(), object_id);
		doc["uniq_id"] = uniq_id;

		if (state_topic && strlen(state_topic) > 0) {
				char stat_t[160];
				if (state_topic[0] == '~') {
						snprintf(stat_t, sizeof(stat_t), "%s", state_topic);
				} else if (state_topic[0] == '/') {
						snprintf(stat_t, sizeof(stat_t), "~%s", state_topic);
				} else {
						snprintf(stat_t, sizeof(stat_t), "~/%s", state_topic);
				}
				doc["stat_t"] = stat_t;
		} else {
				doc["stat_t"] = "~/health/state";
		}

		if (value_template && strlen(value_template) > 0) {
				doc["val_tpl"] = value_template;
		}

		// MQTT binary_sensor expects ON/OFF payloads.
		doc["pl_on"] = "ON";
		doc["pl_off"] = "OFF";

		// Availability
		doc["avty_t"] = "~/availability";
		doc["pl_avail"] = "online";
		doc["pl_not_avail"] = "offline";

		if (device_class && strlen(device_class) > 0) {
				doc["dev_cla"] = device_class;
		}

		fill_device_block(doc, mqtt);

		if (doc.overflowed()) {
				return false;
		}

		return mqtt.publishJson(topic, doc, true);
}

bool ha_discovery_publish_binary_sensor_config_with_topic_suffix(
		MqttManager &mqtt,
		const char *object_id,
		const char *name_suffix,
		const char *state_topic_suffix,
		const char *device_class,
		const char *entity_category
) {
		return ha_discovery_publish_binary_sensor_config(
				mqtt,
				object_id,
				name_suffix,
				nullptr,
				device_class,
				entity_category,
				state_topic_suffix
		);
}

bool ha_discovery_publish_sensor_config(
		MqttManager &mqtt,
		const char *object_id,
		const char *name_suffix,
		const char *value_template,
		const char *unit_of_measurement,
		const char *device_class,
		const char *state_class,
		const char *entity_category
) {
		char topic[160];
		snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s/config", mqtt.sanitizedName(), object_id);

		StaticJsonDocument<768> doc;

		// Use base topic shortcut to keep discovery payload small
		doc["~"] = mqtt.baseTopic();

		// Friendly name in payload
		// Keep entity name short; HA already groups entities under the device name.
		// This also avoids HA generating entity_id values that repeat the device name.
		doc["name"] = name_suffix;

		// Provide a stable object_id that includes the device name once.
		// HA uses this to generate entity_id like: sensor.<sanitized>_<object_id>.
		char ha_object_id[96];
		snprintf(ha_object_id, sizeof(ha_object_id), "%s_%s", mqtt.sanitizedName(), object_id);
		doc["object_id"] = ha_object_id;

		if (entity_category && strlen(entity_category) > 0) {
				doc["entity_category"] = entity_category;
		}

		// Stable unique id (sanitized name + object id)
		char uniq_id[96];
		snprintf(uniq_id, sizeof(uniq_id), "%s_%s", mqtt.sanitizedName(), object_id);
		doc["uniq_id"] = uniq_id;

		doc["stat_t"] = "~/health/state";
		doc["val_tpl"] = value_template;

		// Availability
		doc["avty_t"] = "~/availability";
		doc["pl_avail"] = "online";
		doc["pl_not_avail"] = "offline";

		if (unit_of_measurement && strlen(unit_of_measurement) > 0) {
				doc["unit_of_meas"] = unit_of_measurement;
		}
		if (device_class && strlen(device_class) > 0) {
				doc["dev_cla"] = device_class;
		}
		if (state_class && strlen(state_class) > 0) {
				doc["stat_cla"] = state_class;
		}

		fill_device_block(doc, mqtt);

		if (doc.overflowed()) {
				// Payload too large for this StaticJsonDocument size.
				// mqtt.publishJson() will also fail if serialization exceeds MQTT_MAX_PACKET_SIZE.
				return false;
		}

		return mqtt.publishJson(topic, doc, true);
}

bool ha_discovery_publish_button_event_config(MqttManager &mqtt) {
		char topic[160];
		snprintf(topic, sizeof(topic), "homeassistant/event/%s/button_press/config", mqtt.sanitizedName());

		StaticJsonDocument<768> doc;

		doc["~"] = mqtt.baseTopic();
		doc["name"] = "Button Press";

		char ha_object_id[96];
		snprintf(ha_object_id, sizeof(ha_object_id), "%s_button_press", mqtt.sanitizedName());
		doc["object_id"] = ha_object_id;

		char uniq_id[96];
		snprintf(uniq_id, sizeof(uniq_id), "%s_button_press", mqtt.sanitizedName());
		doc["uniq_id"] = uniq_id;

		doc["stat_t"] = "~/event";

		JsonArray evt_types = doc["event_types"].to<JsonArray>();
		evt_types.add("press");
		evt_types.add("hold");

		doc["avty_t"] = "~/availability";
		doc["pl_avail"] = "online";
		doc["pl_not_avail"] = "offline";

		fill_device_block(doc, mqtt);

		if (doc.overflowed()) {
				return false;
		}

		return mqtt.publishJson(topic, doc, true);
}

bool ha_discovery_publish_screen_select_config(MqttManager &mqtt) {
#if HAS_DISPLAY
		size_t count = 0;
		const ScreenInfo* screens = display_manager_get_available_screens(&count);
		if (!screens || count == 0) return false;

		char topic[160];
		snprintf(topic, sizeof(topic), "homeassistant/select/%s/active_screen/config", mqtt.sanitizedName());

		StaticJsonDocument<1024> doc;

		doc["~"] = mqtt.baseTopic();
		doc["name"] = "Active Screen";

		char ha_object_id[96];
		snprintf(ha_object_id, sizeof(ha_object_id), "%s_active_screen", mqtt.sanitizedName());
		doc["object_id"] = ha_object_id;

		char uniq_id[96];
		snprintf(uniq_id, sizeof(uniq_id), "%s_active_screen", mqtt.sanitizedName());
		doc["uniq_id"] = uniq_id;

		doc["cmd_t"] = "~/screen/set";
		doc["stat_t"] = "~/screen/state";

		JsonArray options = doc["options"].to<JsonArray>();
		for (size_t i = 0; i < count; i++) {
				options.add(screens[i].id);
		}

		doc["avty_t"] = "~/availability";
		doc["pl_avail"] = "online";
		doc["pl_not_avail"] = "offline";

		fill_device_block(doc, mqtt);

		if (doc.overflowed()) return false;

		return mqtt.publishJson(topic, doc, true);
#else
		return false;
#endif
}

// ---------------------------------------------------------------------------
// Audio entities: siren + volume number + beep buttons
// ---------------------------------------------------------------------------

// Helper: fill the common device block used by all entities.
static void fill_device_block(JsonDocument &doc, MqttManager &mqtt) {
		JsonObject dev = doc["dev"].to<JsonObject>();
		JsonArray ids = dev["ids"].to<JsonArray>();
		ids.add(mqtt.sanitizedName());
		dev["name"] = mqtt.friendlyName();
		dev["mdl"] = PROJECT_DISPLAY_NAME;
		dev["sw"] = FIRMWARE_VERSION;
}

static bool publish_siren_config(MqttManager &mqtt) {
		char topic[160];
		snprintf(topic, sizeof(topic), "homeassistant/siren/%s/siren/config", mqtt.sanitizedName());

		StaticJsonDocument<1024> doc;
		doc["~"] = mqtt.baseTopic();
		doc["name"] = "Siren";

		char ha_oid[96];
		snprintf(ha_oid, sizeof(ha_oid), "%s_siren", mqtt.sanitizedName());
		doc["object_id"] = ha_oid;
		doc["uniq_id"] = ha_oid;

		doc["cmd_t"] = "~/audio/siren/set";
		doc["stat_t"] = "~/audio/siren/state";

		// Optimistic false — we publish state after processing.
		doc["optimistic"] = false;

		// Support turn_on parameters: tone, volume_level, duration
		// NOTE: available_tones is required — without it HA's siren
		// platform strips the tone parameter from the MQTT payload entirely.
		// "custom" maps to the pattern stored in the companion text entity.
		doc["support_volume_set"] = true;
		doc["support_duration"]  = true;

		JsonArray tones = doc["available_tones"].to<JsonArray>();
		tones.add("default");
		tones.add("alert");
		tones.add("doorbell");
		tones.add("warning");
		tones.add("custom");

		doc["avty_t"] = "~/availability";
		doc["pl_avail"] = "online";
		doc["pl_not_avail"] = "offline";

		fill_device_block(doc, mqtt);
		if (doc.overflowed()) return false;
		return mqtt.publishJson(topic, doc, true);
}

static bool publish_volume_number_config(MqttManager &mqtt) {
		char topic[160];
		snprintf(topic, sizeof(topic), "homeassistant/number/%s/volume/config", mqtt.sanitizedName());

		StaticJsonDocument<768> doc;
		doc["~"] = mqtt.baseTopic();
		doc["name"] = "Volume";

		char ha_oid[96];
		snprintf(ha_oid, sizeof(ha_oid), "%s_volume", mqtt.sanitizedName());
		doc["object_id"] = ha_oid;
		doc["uniq_id"] = ha_oid;

		doc["cmd_t"] = "~/audio/volume/set";
		doc["stat_t"] = "~/audio/volume/state";

		doc["min"] = 0;
		doc["max"] = 100;
		doc["step"] = 1;
		doc["unit_of_meas"] = "%";
		doc["ic"] = "mdi:volume-high";

		doc["avty_t"] = "~/availability";
		doc["pl_avail"] = "online";
		doc["pl_not_avail"] = "offline";

		fill_device_block(doc, mqtt);
		if (doc.overflowed()) return false;
		return mqtt.publishJson(topic, doc, true);
}

static bool publish_beep_button_config(MqttManager &mqtt, const char *object_id_suffix, const char *name, const char *cmd_topic_suffix) {
		char topic[160];
		snprintf(topic, sizeof(topic), "homeassistant/button/%s/%s/config", mqtt.sanitizedName(), object_id_suffix);

		StaticJsonDocument<768> doc;
		doc["~"] = mqtt.baseTopic();
		doc["name"] = name;

		char ha_oid[96];
		snprintf(ha_oid, sizeof(ha_oid), "%s_%s", mqtt.sanitizedName(), object_id_suffix);
		doc["object_id"] = ha_oid;
		doc["uniq_id"] = ha_oid;

		char cmd_t[80];
		snprintf(cmd_t, sizeof(cmd_t), "~/%s", cmd_topic_suffix);
		doc["cmd_t"] = cmd_t;

		doc["ic"] = "mdi:bell-ring";

		doc["avty_t"] = "~/availability";
		doc["pl_avail"] = "online";
		doc["pl_not_avail"] = "offline";

		fill_device_block(doc, mqtt);
		if (doc.overflowed()) return false;
		return mqtt.publishJson(topic, doc, true);
}

static bool publish_custom_tone_text_config(MqttManager &mqtt) {
		char topic[160];
		snprintf(topic, sizeof(topic), "homeassistant/text/%s/custom_tone/config", mqtt.sanitizedName());

		StaticJsonDocument<768> doc;
		doc["~"] = mqtt.baseTopic();
		doc["name"] = "Custom Tone";

		char ha_oid[96];
		snprintf(ha_oid, sizeof(ha_oid), "%s_custom_tone", mqtt.sanitizedName());
		doc["object_id"] = ha_oid;
		doc["uniq_id"] = ha_oid;

		doc["cmd_t"] = "~/audio/custom_tone/set";
		doc["stat_t"] = "~/audio/custom_tone/state";
		doc["max"] = 127;
		doc["ic"] = "mdi:music-note";

		doc["avty_t"] = "~/availability";
		doc["pl_avail"] = "online";
		doc["pl_not_avail"] = "offline";

		fill_device_block(doc, mqtt);
		if (doc.overflowed()) return false;
		return mqtt.publishJson(topic, doc, true);
}

bool ha_discovery_publish_audio_entities(MqttManager &mqtt) {
		bool ok = true;
		ok &= publish_siren_config(mqtt);
		ok &= publish_volume_number_config(mqtt);
		ok &= publish_custom_tone_text_config(mqtt);
		ok &= publish_beep_button_config(mqtt, "beep",        "Beep",        "audio/beep");
		ok &= publish_beep_button_config(mqtt, "beep_double", "Beep Double", "audio/beep_double");
		ok &= publish_beep_button_config(mqtt, "beep_triple", "Beep Triple", "audio/beep_triple");
		return ok;
}

#endif // HAS_MQTT

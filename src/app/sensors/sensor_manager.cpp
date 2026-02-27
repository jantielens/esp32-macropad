#include "sensor_manager.h"

#include "board_config.h"
#include "log_manager.h"

#if HAS_MQTT
#include "mqtt_manager.h"
#endif

static bool g_sensor_manager_initialized = false;
static bool g_sensor_registry_built = false;

static constexpr size_t kMaxSensors = 8;
static SensorCallbacks g_sensors[kMaxSensors];
static size_t g_sensor_count = 0;

static void ensure_registry_built() {
		if (g_sensor_registry_built) return;

		SensorRegistry registry;
		// Sensor adapters self-register here (see sensors.cpp).
		sensor_manager_register_all(registry);

		g_sensor_registry_built = true;

		if (g_sensor_count == 0) {
				LOGI("Sensor", "No sensors enabled");
		}
}

bool SensorRegistry::add(const SensorCallbacks &callbacks) {
		if (g_sensor_count >= kMaxSensors) {
				LOGW("Sensor", "Sensor registry full (max %u)", (unsigned)kMaxSensors);
				return false;
		}

		g_sensors[g_sensor_count++] = callbacks;
		LOGI("Sensor", "Registered: %s", callbacks.name ? callbacks.name : "(unnamed)");
		return true;
}

void sensor_manager_init() {
		ensure_registry_built();
		if (g_sensor_manager_initialized) return;
		g_sensor_manager_initialized = true;

		for (size_t i = 0; i < g_sensor_count; i++) {
				if (g_sensors[i].init) {
						g_sensors[i].init();
				}
		}
}

void sensor_manager_loop() {
		if (!g_sensor_manager_initialized) {
				sensor_manager_init();
		}

		// Per-sensor loop lets event sensors flush ISR-deferred work.
		for (size_t i = 0; i < g_sensor_count; i++) {
				if (g_sensors[i].loop) {
						g_sensors[i].loop();
				}
		}
}

void sensor_manager_append_api(JsonObject &doc) {
		if (!g_sensor_manager_initialized) {
				sensor_manager_init();
		}

		// Each adapter appends its fields into the shared `sensors` object.
		for (size_t i = 0; i < g_sensor_count; i++) {
				if (g_sensors[i].append_api) {
						g_sensors[i].append_api(doc);
				}
		}
}

void sensor_manager_append_mqtt(JsonObject &doc) {
		if (!g_sensor_manager_initialized) {
				sensor_manager_init();
		}

		// MQTT payload uses the same sensor keys (flat JSON).
		for (size_t i = 0; i < g_sensor_count; i++) {
				if (g_sensors[i].append_mqtt) {
						g_sensors[i].append_mqtt(doc);
				}
		}
}

void sensor_manager_set_number(JsonObject &doc, const char *key, float value, bool valid) {
		if (!key || strlen(key) == 0) return;
		if (valid) {
				doc[key] = value;
		} else {
				doc[key] = nullptr;
		}
}

void sensor_manager_set_bool(JsonObject &doc, const char *key, bool value, bool valid) {
		if (!key || strlen(key) == 0) return;
		if (valid) {
				doc[key] = value;
		} else {
				doc[key] = nullptr;
		}
}


#if HAS_MQTT
void sensor_manager_publish_ha_discovery(MqttManager &mqtt) {
		if (!g_sensor_manager_initialized) {
				sensor_manager_init();
		}

		for (size_t i = 0; i < g_sensor_count; i++) {
				if (g_sensors[i].publish_ha) {
						g_sensors[i].publish_ha(mqtt);
				}
		}
}

// MQTT helpers keep sensor adapters DRY (topic building + ON/OFF payloads).
bool sensor_manager_build_state_topic(const char *state_topic_suffix, char *out_topic, size_t out_len) {
		if (!state_topic_suffix || !out_topic || out_len == 0) return false;
		const char *base = mqtt_manager.baseTopic();
		if (!base || strlen(base) == 0) return false;

		if (state_topic_suffix[0] == '/') {
				return snprintf(out_topic, out_len, "%s%s", base, state_topic_suffix) < (int)out_len;
		}

		return snprintf(out_topic, out_len, "%s/%s", base, state_topic_suffix) < (int)out_len;
}

bool sensor_manager_publish_binary_state(const char *state_topic_suffix, bool on, bool retained) {
		if (!mqtt_manager.connected()) return false;

		char topic[160];
		if (!sensor_manager_build_state_topic(state_topic_suffix, topic, sizeof(topic))) {
				return false;
		}

		// Binary sensors publish ON/OFF payloads on their dedicated topic.
		const char *payload = on ? "ON" : "OFF";
		return mqtt_manager.publishImmediate(topic, payload, retained);
}

#endif

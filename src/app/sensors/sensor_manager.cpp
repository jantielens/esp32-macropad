#include "sensor_manager.h"

#include "board_config.h"
#include "log_manager.h"
#include "ota_activity.h"
#include <math.h>

#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
static StaticSemaphore_t g_cache_mutex_storage;
static SemaphoreHandle_t g_cache_mutex = xSemaphoreCreateMutexStatic(&g_cache_mutex_storage);
class SensorCacheLock {
public:
	SensorCacheLock() { xSemaphoreTake(g_cache_mutex, portMAX_DELAY); }
	~SensorCacheLock() { xSemaphoreGive(g_cache_mutex); }
};
#else
#include <mutex>
static std::mutex g_cache_mutex;
using SensorCacheLock = std::lock_guard<std::mutex>;
#endif

#if HAS_MQTT
#include "mqtt_manager.h"
#endif

static bool g_sensor_manager_initialized = false;
static bool g_sensor_registry_built = false;

static constexpr size_t kMaxSensors = 8;
struct SensorEntry {
	SensorCallbacks callbacks = {};
	JsonDocument readings;
	uint32_t last_sample_ms = 0;
	bool sampled = false;
	bool pending_sample = false;
};
static SensorEntry g_sensors[kMaxSensors];
static size_t g_sensor_count = 0;
static bool g_sampled_once = false;

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
		if (!callbacks.name || !callbacks.sample || !callbacks.fields || !callbacks.field_count ||
				(!callbacks.event_driven && !callbacks.sample_interval_ms)) return false;
		for (size_t field = 0; field < callbacks.field_count; ++field) {
				if (!callbacks.fields[field].key || !callbacks.fields[field].key[0] ||
						!callbacks.fields[field].summary || !callbacks.fields[field].summary[0]) return false;
		}
		if (g_sensor_count >= kMaxSensors) {
				LOGW("Sensor", "Sensor registry full (max %u)", (unsigned)kMaxSensors);
				return false;
		}

		g_sensors[g_sensor_count++].callbacks = callbacks;
		LOGI("Sensor", "Registered: %s", callbacks.name ? callbacks.name : "(unnamed)");
		return true;
}

void sensor_manager_init() {
		ensure_registry_built();
		if (g_sensor_manager_initialized) return;
		g_sensor_manager_initialized = true;

		for (size_t i = 0; i < g_sensor_count; i++) {
				if (g_sensors[i].callbacks.init) {
						g_sensors[i].callbacks.init();
				}
		}
}

static void sample_sensor(SensorEntry &sensor, uint32_t now) {
	if (!sensor.callbacks.sample) return;
	JsonDocument sample;
	JsonObject object = sample.to<JsonObject>();
	sensor.callbacks.sample(object);
#ifdef ARDUINO_ARCH_ESP32
	SensorCacheLock lock;
#else
	SensorCacheLock lock(g_cache_mutex);
#endif
	for (size_t field = 0; field < sensor.callbacks.field_count; ++field) {
		const char *key = sensor.callbacks.fields[field].key;
		const JsonVariantConst value = sample[key];
		if (sensor.readings[key].isUnbound()) sensor.readings[key] = nullptr;
		if (value.is<bool>() || (value.is<double>() && isfinite(value.as<double>()))) {
			sensor.readings[key] = value;
		}
	}
	sensor.last_sample_ms = now;
	sensor.sampled = true;
	sensor.pending_sample = false;
}

void sensor_manager_sample_once() {
	sensor_manager_init();
	if (g_sampled_once || ota_activity_is_active()) return;
	g_sampled_once = true;
	for (size_t index = 0; index < g_sensor_count; ++index) {
		sample_sensor(g_sensors[index], millis());
	}
}

void sensor_manager_loop() {
		if (!g_sensor_manager_initialized) {
				sensor_manager_init();
		}

		// Per-sensor loop lets event sensors flush ISR-deferred work.
		for (size_t i = 0; i < g_sensor_count; i++) {
				bool changed = false;
				if (g_sensors[i].callbacks.loop) {
						changed = g_sensors[i].callbacks.loop();
				}
				const uint32_t now = millis();
				SensorEntry &sensor = g_sensors[i];
				sensor.pending_sample = sensor.pending_sample || changed;
				if (!ota_activity_is_active() && (!sensor.sampled ||
						(sensor.callbacks.event_driven ? sensor.pending_sample :
						(uint32_t)(now - sensor.last_sample_ms) >= sensor.callbacks.sample_interval_ms))) {
						sample_sensor(sensor, now);
				}
		}
}

static void append_cached(JsonObject &doc, bool mqtt) {
#ifdef ARDUINO_ARCH_ESP32
	SensorCacheLock lock;
#else
	SensorCacheLock lock(g_cache_mutex);
#endif
	for (size_t index = 0; index < g_sensor_count; ++index) {
		const SensorEntry &sensor = g_sensors[index];
		if (mqtt && !sensor.callbacks.include_in_mqtt) continue;
		for (size_t field = 0; field < sensor.callbacks.field_count; ++field) {
			const char *key = sensor.callbacks.fields[field].key;
			doc[key] = sensor.readings[key];
		}
	}
}

void sensor_manager_append_api(JsonObject &doc) { append_cached(doc, false); }

void sensor_manager_append_mqtt(JsonObject &doc) {
	append_cached(doc, true);
}

bool sensor_manager_get_number(const char *key, double *value, bool *is_bool) {
	if (!key || !value) return false;
#ifdef ARDUINO_ARCH_ESP32
	SensorCacheLock lock;
#else
	SensorCacheLock lock(g_cache_mutex);
#endif
	for (size_t index = g_sensor_count; index > 0; --index) {
		const SensorEntry &sensor = g_sensors[index - 1];
		bool owns_key = false;
		for (size_t field = 0; field < sensor.callbacks.field_count; ++field) {
			if (strcmp(sensor.callbacks.fields[field].key, key) == 0) owns_key = true;
		}
		if (!owns_key) continue;
		const JsonVariantConst reading = sensor.readings[key];
		if (reading.isNull()) return false;
		const bool boolean = reading.is<bool>();
		if (is_bool) *is_bool = boolean;
		*value = boolean ? (reading.as<bool>() ? 1.0 : 0.0) : reading.as<double>();
		return true;
	}
	return false;
}

size_t sensor_manager_field_count() {
	ensure_registry_built();
	size_t count = 0;
	for (size_t index = 0; index < g_sensor_count; ++index) count += g_sensors[index].callbacks.field_count;
	return count;
}

const SensorField *sensor_manager_field_at(size_t index) {
	ensure_registry_built();
	for (size_t sensor = 0; sensor < g_sensor_count; ++sensor) {
		const SensorCallbacks &callbacks = g_sensors[sensor].callbacks;
		if (index < callbacks.field_count) return &callbacks.fields[index];
		index -= callbacks.field_count;
	}
	return nullptr;
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
				if (g_sensors[i].callbacks.publish_ha) {
						g_sensors[i].callbacks.publish_ha(mqtt);
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

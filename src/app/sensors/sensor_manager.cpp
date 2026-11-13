#include "sensor_manager.h"

#include "board_config.h"
#include "log_manager.h"
#include "ota_activity.h"
#include <atomic>
#include <math.h>

#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
static StaticSemaphore_t g_cache_mutex_storage;
static SemaphoreHandle_t g_cache_mutex = xSemaphoreCreateMutexStatic(&g_cache_mutex_storage);
static StaticSemaphore_t g_registry_mutex_storage;
static SemaphoreHandle_t g_registry_mutex = xSemaphoreCreateMutexStatic(&g_registry_mutex_storage);
class SensorCacheLock {
public:
	explicit SensorCacheLock(SemaphoreHandle_t mutex = g_cache_mutex) : mutex_(mutex) {
		xSemaphoreTake(mutex_, portMAX_DELAY);
	}
	~SensorCacheLock() { xSemaphoreGive(mutex_); }
private:
	SemaphoreHandle_t mutex_;
};
#else
#include <mutex>
static std::mutex g_cache_mutex;
static std::mutex g_registry_mutex;
using SensorCacheLock = std::lock_guard<std::mutex>;
#endif

#if HAS_MQTT
#include "mqtt_manager.h"
#include "ha_discovery.h"
#endif

static std::atomic<bool> g_sensor_manager_initialized{false};
static std::atomic<bool> g_sensor_registry_built{false};

static constexpr size_t kMaxSensors = 8;
struct SensorEntry {
	SensorCallbacks callbacks = {};
	JsonDocument readings;
	uint32_t last_sample_ms = 0;
	bool sampled = false;
	bool pending_sample = false;
#if HAS_BATTERY_INFO
	bool battery_failed = false;
#endif
};
static SensorEntry g_sensors[kMaxSensors];
static size_t g_sensor_count = 0;
static bool g_sampled_once = false;
#if HAS_BATTERY_INFO
static BatteryCache g_battery_cache;
static bool g_battery_registered = false;
static bool g_battery_external_samples = false;
#endif

static void build_registry_locked() {
		if (g_sensor_registry_built.load(std::memory_order_relaxed)) return;

		SensorRegistry registry;
		// Sensor adapters self-register here (see sensors.cpp).
		sensor_manager_register_all(registry);

		g_sensor_registry_built.store(true, std::memory_order_release);

		if (g_sensor_count == 0) {
				LOGI("Sensor", "No sensors enabled");
		}
}

static void ensure_registry_built() {
		if (g_sensor_registry_built.load(std::memory_order_acquire)) return;
		SensorCacheLock lock(g_registry_mutex);
		build_registry_locked();
}

bool SensorRegistry::add(const SensorCallbacks &callbacks) {
#if HAS_BATTERY_INFO
		const bool battery = callbacks.battery != nullptr;
#else
		const bool battery = false;
#endif
		if (!callbacks.name || (!battery && (!callbacks.sample || !callbacks.fields || !callbacks.field_count)) ||
				(!callbacks.event_driven && !callbacks.sample_interval_ms)) return false;
#if HAS_BATTERY_INFO
		if (battery && ((!callbacks.battery->sample && !callbacks.event_driven) || !callbacks.battery->description ||
				!callbacks.battery->fields || !callbacks.sample_interval_ms || g_battery_registered)) {
			LOGE("Sensor", "Invalid or duplicate battery provider");
			return false;
		}
#endif
		for (size_t field = 0; field < callbacks.field_count; ++field) {
				if (!callbacks.fields[field].key || !callbacks.fields[field].key[0] ||
						!callbacks.fields[field].summary || !callbacks.fields[field].summary[0]) return false;
		}
		if (g_sensor_count >= kMaxSensors) {
				LOGW("Sensor", "Sensor registry full (max %u)", (unsigned)kMaxSensors);
				return false;
		}

		g_sensors[g_sensor_count++].callbacks = callbacks;
#if HAS_BATTERY_INFO
		if (battery) {
			g_battery_cache.configure(*callbacks.battery, callbacks.sample_interval_ms);
			g_battery_registered = true;
			g_battery_external_samples = callbacks.battery->sample == nullptr;
		}
#endif
		LOGI("Sensor", "Registered: %s", callbacks.name ? callbacks.name : "(unnamed)");
		return true;
}

void sensor_manager_init() {
		if (g_sensor_manager_initialized.load(std::memory_order_acquire)) return;
		SensorCacheLock lock(g_registry_mutex);
		build_registry_locked();
		if (g_sensor_manager_initialized.load(std::memory_order_relaxed)) return;

		for (size_t i = 0; i < g_sensor_count; i++) {
				if (g_sensors[i].callbacks.init) {
						g_sensors[i].callbacks.init();
				}
		}
		g_sensor_manager_initialized.store(true, std::memory_order_release);
}

static void sample_sensor(SensorEntry &sensor, uint32_t now) {
#if HAS_BATTERY_INFO
	if (sensor.callbacks.battery && !sensor.callbacks.battery->sample) {
		sensor.last_sample_ms = now;
		sensor.sampled = true;
		return;
	}
#endif
	JsonDocument sample;
	JsonObject object = sample.to<JsonObject>();
#if HAS_BATTERY_INFO
	BatterySample battery;
	if (sensor.callbacks.battery) {
		sensor.callbacks.battery->sample(battery);
		bool failed = true;
		for (float value : battery.values) failed = failed && !isfinite(value);
		if (failed && !sensor.battery_failed) LOGW("Sensor", "%s battery read failed", sensor.callbacks.name);
		if (!failed && sensor.battery_failed) LOGI("Sensor", "%s battery readings recovered", sensor.callbacks.name);
		sensor.battery_failed = failed;
	}
	else
#endif
		sensor.callbacks.sample(object);
#ifdef ARDUINO_ARCH_ESP32
	SensorCacheLock lock;
#else
	SensorCacheLock lock(g_cache_mutex);
#endif
#if HAS_BATTERY_INFO
	g_battery_cache.tick(millis(), ota_activity_is_active());
	if (sensor.callbacks.battery) g_battery_cache.update(battery);
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
		sensor_manager_init();
#if HAS_BATTERY_INFO
		{
#ifdef ARDUINO_ARCH_ESP32
			SensorCacheLock lock;
#else
			SensorCacheLock lock(g_cache_mutex);
#endif
			g_battery_cache.tick(millis(), ota_activity_is_active());
		}
#endif

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

#if HAS_BATTERY_INFO
static void append_battery_locked(JsonObject &doc) {
	g_battery_cache.tick(millis(), ota_activity_is_active());
	const BatterySnapshot battery = g_battery_cache.snapshot();
	for (size_t index = 0; index < BATTERY_FIELD_COUNT; ++index) {
		const BatteryField field = static_cast<BatteryField>(index);
		if (!(battery.supported & battery_field_mask(field))) continue;
		const char *key = battery_field_info(field).json_key;
		const float value = battery.readings.get(field);
		if (!isfinite(value)) doc[key] = nullptr;
		else if (field == BatteryField::Status) doc[key] = battery_status_text(value);
		else if (field == BatteryField::ExternalPowerPresent) doc[key] = value != 0;
		else doc[key] = value;
	}
}

void sensor_manager_append_battery_mqtt(JsonObject &doc) {
	ensure_registry_built();
#ifdef ARDUINO_ARCH_ESP32
	SensorCacheLock lock;
#else
	SensorCacheLock lock(g_cache_mutex);
#endif
	append_battery_locked(doc);
}
#endif

static void append_cached(JsonObject &doc, bool mqtt) {
	ensure_registry_built();
#ifdef ARDUINO_ARCH_ESP32
	SensorCacheLock lock;
#else
	SensorCacheLock lock(g_cache_mutex);
#endif
#if HAS_BATTERY_INFO
	append_battery_locked(doc);
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

#if HAS_BATTERY_INFO
void sensor_manager_get_battery(BatterySnapshot &snapshot) {
	ensure_registry_built();
#ifdef ARDUINO_ARCH_ESP32
	SensorCacheLock lock;
#else
	SensorCacheLock lock(g_cache_mutex);
#endif
	g_battery_cache.tick(millis(), ota_activity_is_active());
	snapshot = g_battery_cache.snapshot();
}

void sensor_manager_record_battery(const BatterySample &sample) {
	ensure_registry_built();
#ifdef ARDUINO_ARCH_ESP32
	SensorCacheLock lock;
#else
	SensorCacheLock lock(g_cache_mutex);
#endif
	if (!g_battery_external_samples) {
		LOGE("Sensor", "Battery sample received without an externally sampled provider");
		return;
	}
	g_battery_cache.tick(millis(), ota_activity_is_active());
	g_battery_cache.update(sample);
}
#endif

void sensor_manager_append_mqtt(JsonObject &doc) {
	append_cached(doc, true);
}

bool sensor_manager_get_number(const char *key, double *value, bool *is_bool) {
	if (!key || !value) return false;
	ensure_registry_built();
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
		sensor_manager_init();

		for (size_t i = 0; i < g_sensor_count; i++) {
#if HAS_BATTERY_INFO
				if (g_sensors[i].callbacks.battery) {
					const uint32_t supported = battery_supported_fields(g_sensors[i].callbacks.battery->fields);
					static const char *names[] = {"Battery Voltage", "Battery", "Battery Current",
						"Battery Power", "Battery Discharge Power", "Battery Status", "External Power",
						"Battery Time Remaining", "Battery Time To Full"};
					for (size_t field_index = 0; field_index < BATTERY_FIELD_COUNT; ++field_index) {
						const BatteryField field = static_cast<BatteryField>(field_index);
						if (!(supported & battery_field_mask(field))) continue;
						const BatteryFieldInfo &info = battery_field_info(field);
						char value_template[160];
						if (field == BatteryField::ExternalPowerPresent) {
							snprintf(value_template, sizeof(value_template),
								"{{ 'None' if value_json.%s is none else ('ON' if value_json.%s else 'OFF') }}",
								info.json_key, info.json_key);
							ha_discovery_publish_binary_sensor_config(mqtt, info.json_key, names[field_index],
								value_template, "power", "diagnostic");
						} else {
							snprintf(value_template, sizeof(value_template), "{{ value_json.%s }}", info.json_key);
							const char *device_class = field == BatteryField::Voltage ? "voltage" :
								field == BatteryField::Percentage ? "battery" :
								field == BatteryField::Current ? "current" :
								field == BatteryField::Power || field == BatteryField::DischargePower ? "power" :
								field == BatteryField::TimeRemaining || field == BatteryField::TimeToFull ? "duration" : nullptr;
							ha_discovery_publish_sensor_config(mqtt, info.json_key, names[field_index], value_template,
								field == BatteryField::Status ? nullptr : info.unit, device_class,
								field == BatteryField::Status ? nullptr : "measurement", "diagnostic");
						}
					}
				}
#endif
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

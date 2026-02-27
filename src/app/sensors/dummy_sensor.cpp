#include "dummy_sensor.h"

#if HAS_SENSOR_DUMMY

#include "log_manager.h"
#include "ha_discovery.h"
#include "sensor_manager.h"
#include <esp_system.h>

static DummySensor g_dummy_sensor;

bool DummySensor::begin() {
		if (_initialized) return _available;
		_initialized = true;

		// Seed Arduino RNG with hardware entropy for repeatable test variance.
		randomSeed((unsigned long)esp_random());

		_available = true;
		LOGI("Sensor", "Dummy sensor enabled");
		return true;
}

void DummySensor::update() {
		// Generate a synthetic value in a stable range for dashboards.
		const long r = random(0, 10000); // 0..9999
		_value = (float)r / 100.0f;      // 0.00..99.99
}

void DummySensor::appendJson(JsonObject &doc) {
		if (!_available) return;

		update();
		sensor_manager_set_number(doc, "dummy_value", _value, true);
}

#if HAS_MQTT
void DummySensor::publishHaDiscovery(MqttManager &mqtt) {
		ha_discovery_publish_sensor_config(
				mqtt,
				"dummy_value",
				"Dummy Value",
				"{{ value_json.dummy_value }}",
				"",
				nullptr,
				nullptr,
				"diagnostic"
		);
}
#endif

static void dummy_init() {
		g_dummy_sensor.begin();
}

static void dummy_append_api(JsonObject &doc) {
		g_dummy_sensor.appendJson(doc);
}

static void dummy_append_mqtt(JsonObject &doc) {
		g_dummy_sensor.appendJson(doc);
}

#if HAS_MQTT
static void dummy_publish_ha(MqttManager &mqtt) {
		g_dummy_sensor.publishHaDiscovery(mqtt);
}
#endif

void register_dummy_sensor(SensorRegistry &registry) {
		SensorCallbacks callbacks = {};
		callbacks.name = "DUMMY";
		callbacks.init = dummy_init;
		callbacks.append_api = dummy_append_api;
		callbacks.append_mqtt = dummy_append_mqtt;
#if HAS_MQTT
		callbacks.publish_ha = dummy_publish_ha;
#endif
		registry.add(callbacks);
}

#endif // HAS_SENSOR_DUMMY

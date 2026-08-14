#include "aht10_sensor.h"

#if HAS_SENSOR_AHT10

#include "../ble_telemetry.h"
#include "ha_discovery.h"
#include "log_manager.h"
#include "sensor_manager.h"
#include <Adafruit_AHTX0.h>
#include <Wire.h>

static Adafruit_AHTX0 g_aht10;
static Aht10Sensor g_aht10_adapter;
static bool g_i2c_initialized = false;

static void sensor_i2c_begin_once() {
	if (g_i2c_initialized) return;

	if (SENSOR_I2C_SDA >= 0 && SENSOR_I2C_SCL >= 0) {
		Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL);
	} else {
		Wire.begin();
	}
	Wire.setClock(SENSOR_I2C_FREQUENCY);
	g_i2c_initialized = true;
}

bool Aht10Sensor::begin() {
	if (_initialized) return _available;
	_initialized = true;
	sensor_i2c_begin_once();

	_available = g_aht10.begin(&Wire);
	if (_available) {
		LOGI("Sensor", "AHT10 ready at 0x38");
	} else {
		LOGW("Sensor", "AHT10 not found at 0x38");
	}
	return _available;
}

void Aht10Sensor::update() {
	if (!_available) return;

	sensors_event_t humidity_event;
	sensors_event_t temperature_event;
	if (!g_aht10.getEvent(&humidity_event, &temperature_event)) {
		_has_valid_readings = false;
		return;
	}

	_temperature_c = temperature_event.temperature;
	_humidity_pct = humidity_event.relative_humidity;
	_has_valid_readings = !(isnan(_temperature_c) || isnan(_humidity_pct));
	if (!_has_valid_readings) return;

#if HAS_BLE
	ble_telemetry_set_s16(0x02, (int16_t)(_temperature_c * 100.0f));
	ble_telemetry_set_u16(0x03, (uint16_t)(_humidity_pct * 100.0f));
#endif
}

void Aht10Sensor::appendJson(JsonObject &doc) {
	if (available()) update();
	const bool valid = available() && hasValidReadings();
	sensor_manager_set_number(doc, "temperature", temperatureC(), valid);
	sensor_manager_set_number(doc, "humidity", humidityPct(), valid);
}

#if HAS_MQTT
void Aht10Sensor::publishHaDiscovery(MqttManager &mqtt) {
	ha_discovery_publish_sensor_config(mqtt, "temperature", "Temperature", "{{ value_json.temperature }}", "°C", "temperature", "measurement", nullptr);
	ha_discovery_publish_sensor_config(mqtt, "humidity", "Humidity", "{{ value_json.humidity }}", "%", "humidity", "measurement", nullptr);
}
#endif

static void aht10_init() { g_aht10_adapter.begin(); }
static void aht10_append_api(JsonObject &doc) { g_aht10_adapter.appendJson(doc); }
static void aht10_append_mqtt(JsonObject &doc) { g_aht10_adapter.appendJson(doc); }
#if HAS_MQTT
static void aht10_publish_ha(MqttManager &mqtt) { g_aht10_adapter.publishHaDiscovery(mqtt); }
#endif

void register_aht10_sensor(SensorRegistry &registry) {
	SensorCallbacks callbacks = {};
	callbacks.name = "AHT10";
	callbacks.init = aht10_init;
	callbacks.append_api = aht10_append_api;
	callbacks.append_mqtt = aht10_append_mqtt;
#if HAS_MQTT
	callbacks.publish_ha = aht10_publish_ha;
#endif
	registry.add(callbacks);
}

#endif // HAS_SENSOR_AHT10
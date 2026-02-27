#include "bme280_sensor.h"

#if HAS_SENSOR_BME280

#include "log_manager.h"
#include "ha_discovery.h"
#include "sensor_manager.h"
#include <Adafruit_BME280.h>
#include <Wire.h>
#include <math.h>

static Adafruit_BME280 g_bme280;
static bool g_i2c_initialized = false;
static Bme280Sensor g_bme280_adapter;

static void sensor_i2c_begin_once() {
		if (g_i2c_initialized) return;

		// Shared I2C bus for sensors; init once to avoid reconfiguration churn.
		if (SENSOR_I2C_SDA >= 0 && SENSOR_I2C_SCL >= 0) {
				Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL);
		} else {
				Wire.begin();
		}

		Wire.setClock(SENSOR_I2C_FREQUENCY);
		g_i2c_initialized = true;
}

bool Bme280Sensor::begin() {
		if (_initialized) return _available;
		_initialized = true;

		// BME280 shares the global sensor I2C bus.
		sensor_i2c_begin_once();

		const bool ok = g_bme280.begin((uint8_t)BME280_I2C_ADDR, &Wire);
		if (!ok) {
				_available = false;
				LOGW("Sensor", "BME280 not found at 0x%02X", (unsigned)BME280_I2C_ADDR);
				return false;
		}

		_available = true;
		LOGI("Sensor", "BME280 ready at 0x%02X", (unsigned)BME280_I2C_ADDR);
		return true;
}

void Bme280Sensor::update() {
		if (!_available) return;

		// Synchronous read; called on demand by appendJson().
		_temperature_c = g_bme280.readTemperature();
		_humidity_pct = g_bme280.readHumidity();
		_pressure_hpa = g_bme280.readPressure() / 100.0f;

		_has_valid_readings = !(isnan(_temperature_c) || isnan(_humidity_pct) || isnan(_pressure_hpa));

		if (_has_valid_readings) {
				LOGI(
						"Sensor",
						"BME280 read: %.2f C, %.2f %%RH, %.2f hPa",
						_temperature_c,
						_humidity_pct,
						_pressure_hpa
				);
		}
}

void Bme280Sensor::appendJson(JsonObject &doc) {
		if (available()) {
				// On-demand sampling keeps the framework simple (API/MQTT read the same cache).
				update();
		}

		const bool valid = available() && hasValidReadings();

		if (valid) {
				sensor_manager_set_number(doc, "temperature", temperatureC(), true);
				sensor_manager_set_number(doc, "humidity", humidityPct(), true);
				sensor_manager_set_number(doc, "pressure", pressureHpa(), true);
				return;
		}

		// Sensor missing: emit min-range sentinel values that fit BTHome encoding.
		doc["temperature"] = -327.68f;
		doc["humidity"] = 0.0f;
		doc["pressure"] = 0.0f;
}

#if HAS_MQTT
void Bme280Sensor::publishHaDiscovery(MqttManager &mqtt) {
		// MQTT state is the shared health JSON; HA uses value_template to extract fields.
		ha_discovery_publish_sensor_config(mqtt, "temperature", "Temperature", "{{ value_json.temperature }}", "°C", "temperature", "measurement", nullptr);
		ha_discovery_publish_sensor_config(mqtt, "humidity", "Humidity", "{{ value_json.humidity }}", "%", "humidity", "measurement", nullptr);
		ha_discovery_publish_sensor_config(mqtt, "pressure", "Pressure", "{{ value_json.pressure }}", "hPa", "pressure", "measurement", nullptr);
}
#endif

static void bme280_init() {
		g_bme280_adapter.begin();
}

static void bme280_append_api(JsonObject &doc) {
		g_bme280_adapter.appendJson(doc);
}

static void bme280_append_mqtt(JsonObject &doc) {
		g_bme280_adapter.appendJson(doc);
}


#if HAS_MQTT
static void bme280_publish_ha(MqttManager &mqtt) {
		g_bme280_adapter.publishHaDiscovery(mqtt);
}
#endif

void register_bme280_sensor(SensorRegistry &registry) {
		SensorCallbacks callbacks = {};
		callbacks.name = "BME280";
		callbacks.init = bme280_init;
		callbacks.append_api = bme280_append_api;
		callbacks.append_mqtt = bme280_append_mqtt;
#if HAS_MQTT
		callbacks.publish_ha = bme280_publish_ha;
#endif

		registry.add(callbacks);
}

#endif // HAS_SENSOR_BME280

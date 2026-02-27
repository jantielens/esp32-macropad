#ifndef BME280_SENSOR_H
#define BME280_SENSOR_H

#include "board_config.h"

#include <Arduino.h>
#include <math.h>

#include <ArduinoJson.h>

class Bme280Sensor {
public:
		bool begin();
		void update();
		void appendJson(JsonObject &doc);

#if HAS_MQTT
		void publishHaDiscovery(class MqttManager &mqtt);
#endif

		bool available() const { return _available; }
		bool hasValidReadings() const { return _has_valid_readings; }

		float temperatureC() const { return _temperature_c; }
		float humidityPct() const { return _humidity_pct; }
		float pressureHpa() const { return _pressure_hpa; }

private:
		bool _initialized = false;
		bool _available = false;
		bool _has_valid_readings = false;

		float _temperature_c = NAN;
		float _humidity_pct = NAN;
		float _pressure_hpa = NAN;

};

class SensorRegistry;
void register_bme280_sensor(SensorRegistry &registry);

#endif // BME280_SENSOR_H

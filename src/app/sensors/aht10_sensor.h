#ifndef AHT10_SENSOR_H
#define AHT10_SENSOR_H

#include "board_config.h"

#if HAS_SENSOR_AHT10

#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>

class Aht10Sensor {
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

private:
	bool _initialized = false;
	bool _available = false;
	bool _has_valid_readings = false;
	float _temperature_c = NAN;
	float _humidity_pct = NAN;
};

class SensorRegistry;
void register_aht10_sensor(SensorRegistry &registry);

#endif // HAS_SENSOR_AHT10

#endif // AHT10_SENSOR_H
#ifndef BATTERY_ADC_SENSOR_H
#define BATTERY_ADC_SENSOR_H

#include "board_config.h"
#include <stdint.h>

// Approximate a resting single-cell LiPo state of charge from cell voltage.
// Values between curve points are linearly interpolated.
uint8_t battery_adc_lipo_percentage(float voltage);

#if HAS_SENSOR_BATTERY_ADC

#include <Arduino.h>
#include <ArduinoJson.h>

class BatteryAdcSensor {
public:
	bool begin();
	void update();
	void appendJson(JsonObject &doc);

#if HAS_MQTT
	void publishHaDiscovery(class MqttManager &mqtt);
#endif

	bool available() const { return _available; }
	bool hasValidReading() const { return _has_valid_reading; }
	float voltage() const { return _voltage; }
	uint8_t percentage() const { return _percentage; }

private:
	bool _available = false;
	bool _has_valid_reading = false;
	float _voltage = 0.0f;
	uint8_t _percentage = 0;
};

class SensorRegistry;
void register_battery_adc_sensor(SensorRegistry &registry);

#endif // HAS_SENSOR_BATTERY_ADC

#endif // BATTERY_ADC_SENSOR_H
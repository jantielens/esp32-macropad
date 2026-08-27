#include "battery_adc_sensor.h"

#include <math.h>

namespace {
constexpr uint8_t kCurvePercentages[] = {0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 95, 100};
constexpr float kCurveVoltages[] = {3.00f, 3.30f, 3.50f, 3.61f, 3.69f, 3.75f, 3.79f, 3.83f, 3.87f, 3.95f, 4.11f, 4.15f, 4.20f};
constexpr size_t kCurvePointCount = sizeof(kCurveVoltages) / sizeof(kCurveVoltages[0]);
} // namespace

uint8_t battery_adc_lipo_percentage(float voltage) {
	if (!isfinite(voltage) || voltage <= kCurveVoltages[0]) return 0;
	if (voltage >= kCurveVoltages[kCurvePointCount - 1]) return 100;

	for (size_t i = 1; i < kCurvePointCount; ++i) {
		if (voltage > kCurveVoltages[i]) continue;
		const float fraction = (voltage - kCurveVoltages[i - 1]) /
				(kCurveVoltages[i] - kCurveVoltages[i - 1]);
		const float percentage = kCurvePercentages[i - 1] +
				fraction * (kCurvePercentages[i] - kCurvePercentages[i - 1]);
		return (uint8_t)lroundf(percentage);
	}

	return 100;
}

#if HAS_SENSOR_BATTERY_ADC

#include "../ble_telemetry.h"
#include "ha_discovery.h"
#include "log_manager.h"
#include "sensor_manager.h"

namespace {
constexpr float kMinimumValidVoltage = 2.5f;
constexpr float kMaximumValidVoltage = 4.35f;
static BatteryAdcSensor g_battery_adc_adapter;
} // namespace

bool BatteryAdcSensor::begin() {
	if (BATTERY_ADC_PIN < 0 || BATTERY_ADC_DIVIDER <= 0.0f ||
			BATTERY_ADC_CALIBRATION <= 0.0f || BATTERY_ADC_SAMPLE_COUNT == 0) {
		LOGW("Sensor", "Battery ADC configuration is invalid");
		return false;
	}

	pinMode(BATTERY_ADC_PIN, INPUT);
	analogReadResolution(12);
	analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
	_available = true;
	LOGI("Sensor", "Battery ADC ready on GPIO%d (divider %.3f)", BATTERY_ADC_PIN, (double)BATTERY_ADC_DIVIDER);
	return true;
}

void BatteryAdcSensor::update() {
	if (!_available) return;

	uint32_t millivolts = 0;
	for (uint8_t i = 0; i < BATTERY_ADC_SAMPLE_COUNT; ++i) {
		millivolts += analogReadMilliVolts(BATTERY_ADC_PIN);
	}

	_voltage = ((float)millivolts / BATTERY_ADC_SAMPLE_COUNT) / 1000.0f;
	_voltage *= BATTERY_ADC_DIVIDER * BATTERY_ADC_CALIBRATION;
	_has_valid_reading = isfinite(_voltage) && _voltage >= kMinimumValidVoltage &&
			_voltage <= kMaximumValidVoltage;
	if (!_has_valid_reading) return;

	_percentage = battery_adc_lipo_percentage(_voltage);
#if HAS_BLE
	ble_telemetry_set_u8(0x01, _percentage);
	ble_telemetry_set_u16(0x0C, (uint16_t)lroundf(_voltage * 1000.0f));
#endif
}

void BatteryAdcSensor::appendJson(JsonObject &doc) {
	update();
	sensor_manager_set_number(doc, "battery_voltage", voltage(), hasValidReading());
	if (hasValidReading()) {
		doc["battery_percentage"] = percentage();
	} else {
		doc["battery_percentage"] = nullptr;
	}
}

#if HAS_MQTT
void BatteryAdcSensor::publishHaDiscovery(MqttManager &mqtt) {
	ha_discovery_publish_sensor_config(mqtt, "battery_voltage", "Battery Voltage", "{{ value_json.battery_voltage }}", "V", "voltage", "measurement", "diagnostic");
	ha_discovery_publish_sensor_config(mqtt, "battery_percentage", "Battery", "{{ value_json.battery_percentage }}", "%", "battery", "measurement", nullptr);
}
#endif

static void battery_adc_init() { g_battery_adc_adapter.begin(); }
static void battery_adc_append_api(JsonObject &doc) { g_battery_adc_adapter.appendJson(doc); }
static void battery_adc_append_mqtt(JsonObject &doc) { g_battery_adc_adapter.appendJson(doc); }
#if HAS_MQTT
static void battery_adc_publish_ha(MqttManager &mqtt) { g_battery_adc_adapter.publishHaDiscovery(mqtt); }
#endif

void register_battery_adc_sensor(SensorRegistry &registry) {
	SensorCallbacks callbacks = {};
	callbacks.name = "Battery ADC";
	callbacks.init = battery_adc_init;
	callbacks.append_api = battery_adc_append_api;
	callbacks.append_mqtt = battery_adc_append_mqtt;
#if HAS_MQTT
	callbacks.publish_ha = battery_adc_publish_ha;
#endif
	registry.add(callbacks);
}

#endif // HAS_SENSOR_BATTERY_ADC
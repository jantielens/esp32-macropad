#define BOARD_CONFIG_H
#ifndef HAS_MQTT
#define HAS_MQTT 0
#endif
#define HAS_DISPLAY 1
#define HAS_SENSOR_BATTERY_ADC HAS_BATTERY_INFO
#define BATTERY_ADC_PIN 1
#define BATTERY_ADC_DIVIDER 2.0f
#define BATTERY_ADC_CALIBRATION 1.0f
#define BATTERY_ADC_SAMPLE_COUNT 4
#define INPUT 0
#define ADC_11db 3
#if HAS_MQTT
#define MQTT_MANAGER_H
class MqttManager {
public:
	bool connected() { return true; }
	const char *baseTopic() const { return "test"; }
	bool publishImmediate(const char *, const char *, bool) { return true; }
};
MqttManager mqtt_manager;
#endif
#include "log_manager.h"
#include "../src/app/sensors/sensor_manager.cpp"
#include "../src/app/sensor_binding.cpp"
#include "../src/app/battery_binding.cpp"
#include "../src/app/binding_template.cpp"
#undef TAG
#include "../src/app/expr_binding.cpp"
#include "../src/app/expr_eval.cpp"
#include <cassert>
#include <limits>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

#if HAS_BATTERY_INFO
static uint32_t adc_millivolts = 1900;
static unsigned adc_samples = 0;
void pinMode(int, int) {}
void analogReadResolution(int) {}
void analogSetPinAttenuation(int, int) {}
uint32_t analogReadMilliVolts(int) { ++adc_samples; return adc_millivolts; }
#include "../src/app/sensors/battery_adc_sensor.cpp"
#endif

static unsigned long now_ms = 0;
static unsigned fast_samples = 0;
static unsigned slow_samples = 0;
static unsigned event_samples = 0;
static bool failed = false;
static bool event_state = false;
static bool ota_active = false;
#if HAS_BATTERY_INFO
static bool battery_profile = false;
static bool battery_adc_profile = false;
static bool battery_frame_profile = false;
static unsigned battery_samples = 0;
static BatterySample battery_fixture;
static bool fail_metadata_alloc = false;
void *config_psram_alloc(size_t bytes, const char *) {
	return fail_metadata_alloc ? nullptr : calloc(1, bytes);
}
#endif
static bool concurrent_profile = false;
static std::atomic<unsigned> registry_calls{0};
static std::atomic<unsigned> init_calls{0};
static std::atomic<bool> registry_entered{false};
static std::atomic<bool> allow_registry{false};
static std::atomic<bool> init_entered{false};
static std::atomic<bool> allow_init{false};
unsigned long millis() { return now_ms; }
bool ota_activity_is_active() { return ota_active; }

void sensor_manager_register_all(SensorRegistry &registry) {
	++registry_calls;
	if (concurrent_profile) {
		registry_entered = true;
		while (!allow_registry) std::this_thread::yield();
	}
#if HAS_BATTERY_INFO
	if (battery_adc_profile) register_battery_adc_sensor(registry);
	if (battery_frame_profile) {
		static const BatteryProvider provider = {
			"Driver-owned battery samples.",
			battery_field_mask(BatteryField::Voltage) | battery_field_mask(BatteryField::Percentage),
			nullptr,
		};
		SensorCallbacks battery = {};
		battery.name = "frame";
		battery.battery = &provider;
		battery.event_driven = true;
		battery.sample_interval_ms = 60000;
		assert(registry.add(battery));
	}
	if (battery_profile) {
		static const BatteryProvider provider = {
			"Test battery provider.",
			battery_field_mask(BatteryField::Voltage) | battery_field_mask(BatteryField::Percentage) |
			battery_field_mask(BatteryField::Current) | battery_field_mask(BatteryField::Status) |
			battery_field_mask(BatteryField::TimeRemaining) | battery_field_mask(BatteryField::TimeToFull),
			[](BatterySample &sample) { ++battery_samples; sample = battery_fixture; },
		};
		SensorCallbacks battery = {};
		battery.name = "battery";
		battery.battery = &provider;
		assert(registry.add(battery));
		assert(!registry.add(battery));
	}
#endif
	static const SensorField fields[] = {{"temperature", "Temperature in Celsius."}};
	SensorCallbacks fast = {};
	fast.name = "fast";
	fast.init = []() {
		++init_calls;
		if (concurrent_profile) {
			init_entered = true;
			while (!allow_init) std::this_thread::yield();
		}
	};
	fast.fields = fields;
	fast.field_count = 1;
	fast.sample = [](JsonObject &doc) {
		++fast_samples;
		sensor_manager_set_number(doc, "temperature", 12.5f, !failed);
	};
	assert(registry.add(fast));
	SensorCallbacks slow = fast;
	slow.name = "slow";
	slow.init = nullptr;
	slow.sample_interval_ms = 10000;
	slow.sample = [](JsonObject &doc) {
		++slow_samples;
		sensor_manager_set_number(doc, "temperature", failed ? std::numeric_limits<float>::quiet_NaN() : 24.5f, true);
	};
	assert(registry.add(slow));
	static const SensorField event_fields[] = {{"presence", "Presence"}};
	SensorCallbacks event = {};
	event.name = "event";
	event.fields = event_fields;
	event.field_count = 1;
	event.event_driven = true;
	event.loop = []() {
		static bool previous = false;
		const bool changed = previous != event_state;
		previous = event_state;
		return changed;
	};
	event.include_in_mqtt = false;
	event.sample = [](JsonObject &doc) {
		++event_samples;
		sensor_manager_set_bool(doc, "presence", event_state, true);
	};
	assert(registry.add(event));
}

int main(int argc, char **argv) {
#if HAS_BATTERY_INFO
	battery_profile = argc > 1 && strcmp(argv[1], "--battery") == 0;
	battery_adc_profile = argc > 1 && strcmp(argv[1], "--battery-adc") == 0;
	battery_frame_profile = argc > 1 && strcmp(argv[1], "--battery-frame") == 0;
	if (argc > 1 && strcmp(argv[1], "--concurrent-init") == 0) {
		concurrent_profile = true;
		battery_frame_profile = true;
		std::thread first([] { sensor_manager_init(); });
		while (!registry_entered) std::this_thread::yield();
		std::atomic<bool> second_started{false};
		std::thread second([&] {
			second_started = true;
			BatterySample sample;
			sample.set(BatteryField::Voltage, 3.8f);
			sensor_manager_record_battery(sample);
		});
		while (!second_started) std::this_thread::yield();
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		assert(registry_calls == 1);
		allow_registry = true;
		while (!init_entered) std::this_thread::yield();
		std::atomic<bool> third_done{false};
		std::thread third([&] { sensor_manager_init(); third_done = true; });
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		assert(!third_done);
		allow_init = true;
		first.join();
		second.join();
		third.join();
		assert(registry_calls == 1 && init_calls == 1);
		BatterySnapshot snapshot;
		sensor_manager_get_battery(snapshot);
		assert(snapshot.readings.get(BatteryField::Voltage) == 3.8f);
		return 0;
	}
#endif
	sensor_manager_init();
#if HAS_BATTERY_INFO
	if (battery_frame_profile) {
		battery_binding_init();
		sensor_manager_sample_once();
		JsonDocument telemetry;
		JsonObject object = telemetry.to<JsonObject>();
		telemetry["battery_mv"] = 3800;
		telemetry["battery_pct"] = 65;
		sensor_manager_append_battery_mqtt(object);
		assert(telemetry["battery_voltage"].isNull());
		assert(telemetry["battery_percentage"].isNull());
		BatterySample sample;
		sample.set(BatteryField::Voltage, 3.8f);
		sample.set(BatteryField::Percentage, 65);
		sensor_manager_record_battery(sample);
		sensor_manager_append_battery_mqtt(object);
		assert(telemetry["battery_voltage"].as<float>() == 3.8f);
		assert(telemetry["battery_percentage"].as<int>() == 65);
		assert(telemetry["battery_mv"].as<int>() == 3800);
		assert(telemetry["battery_pct"].as<int>() == 65);
		assert(telemetry["temperature"].isUnbound());
		now_ms = 179999;
		sensor_manager_loop();
		char resolved[64];
		binding_template_resolve("[battery:voltage;%.2f]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "3.80") == 0);
		assert(battery_samples == 0 && adc_samples == 0);
		++now_ms;
		sensor_manager_append_battery_mqtt(object);
		assert(telemetry["battery_voltage"].isNull());
		assert(telemetry["battery_percentage"].isNull());
		binding_template_resolve("[battery:percentage|N/A]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "N/A") == 0);
		sensor_manager_record_battery(sample);
		binding_template_resolve("[battery:percentage;%.0f]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "65") == 0);
		return 0;
	}
	if (battery_adc_profile) {
		battery_binding_init();
		sensor_manager_sample_once();
		char resolved[64];
		binding_template_resolve("[battery:voltage;%.2f]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "3.80") == 0);
		binding_template_resolve("[battery:percentage;%.0f]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "53") == 0);
		binding_template_resolve("[battery:current|N/A]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "N/A") == 0);
		assert(adc_samples == 4 && sensor_manager_field_count() == 3);
		adc_millivolts = 0;
		now_ms = 6000;
		sensor_manager_loop();
		binding_template_resolve("[battery:voltage|N/A]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "N/A") == 0);
		return 0;
	}
	if (battery_profile) {
		fail_metadata_alloc = true;
		battery_binding_init();
		assert(!binding_template_scheme_known("battery", 7));
		fail_metadata_alloc = false;
		battery_binding_init();
		char resolved[64];
		binding_template_resolve("[battery:current;%.3f|N/A]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "N/A") == 0);
		battery_fixture.set(BatteryField::Voltage, 4);
		battery_fixture.set(BatteryField::Percentage, 73);
		battery_fixture.set(BatteryField::Current, -0.25);
		battery_fixture.set_status(BatteryStatus::Discharging);
		battery_fixture.set(BatteryField::TimeRemaining, 120);
		sensor_manager_sample_once();
		binding_template_resolve("Draw: [battery:discharge_power;%.2f] W", resolved, sizeof(resolved));
		assert(strcmp(resolved, "Draw: 1.00 W") == 0);
		binding_template_resolve("[battery:status]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "Discharging") == 0);
		binding_template_resolve("[battery:external_power_present|N/A]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "N/A") == 0);
		assert(binding_template_validate_params("battery", 7, "external_power_present") == nullptr);
		assert(binding_template_validate_params("battery", 7, "curent") != nullptr);
		assert(battery_samples == 1);
		JsonDocument telemetry;
		JsonObject object = telemetry.to<JsonObject>();
		sensor_manager_append_api(object);
		assert(telemetry["battery_power"].as<float>() == -1);
		assert(strcmp(telemetry["battery_status"].as<const char *>(), "Discharging") == 0);
		battery_fixture.set_status(BatteryStatus::Charging);
		battery_fixture.set(BatteryField::Current, 0.5);
		battery_fixture.set(BatteryField::TimeToFull, 45);
		now_ms = 2000;
		sensor_manager_loop();
		binding_template_resolve("[battery:time_remaining;%.0f|N/A]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "N/A") == 0);
		binding_template_resolve("[battery:time_to_full;%.0f]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "45") == 0);
		binding_template_resolve("[battery:discharge_power]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "0") == 0);
		battery_fixture = {};
		now_ms = 4000;
		sensor_manager_loop();
		binding_template_resolve("[battery:current;%.1f]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "0.5") == 0);
		binding_template_resolve("[battery:status|Unknown]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "Unknown") == 0);
		ota_active = true;
		sensor_manager_loop();
		now_ms = 100000;
		sensor_manager_loop();
		binding_template_resolve("[battery:current;%.1f]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "0.5") == 0);
		assert(battery_samples == 3);
		ota_active = false;
		sensor_manager_loop();
		now_ms += 3999;
		binding_template_resolve("[battery:current;%.1f]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "0.5") == 0);
		++now_ms;
		binding_template_resolve("[battery:current|N/A]", resolved, sizeof(resolved));
		assert(strcmp(resolved, "N/A") == 0);
		sensor_manager_append_mqtt(object);
		assert(telemetry["battery_current"].isNull());

		BatteryCache cache;
		const BatteryProvider adc = {"ADC-only fixture", battery_field_mask(BatteryField::Voltage), nullptr};
		cache.configure(adc, 2000);
		cache.tick(0xfffffff0, false);
		BatterySample sample;
		sample.set(BatteryField::Voltage, 3.8f);
		sample.set(BatteryField::Current, 1);
		cache.update(sample);
		cache.tick(1984, false);
		assert(cache.snapshot().readings.get(BatteryField::Voltage) == 3.8f);
		assert(!isfinite(cache.snapshot().readings.get(BatteryField::Current)));
		cache.tick(5984, false);
		assert(!isfinite(cache.snapshot().readings.get(BatteryField::Voltage)));
		BatteryCache flow;
		const BatteryProvider gauge = {"Gauge", battery_supported_fields(
			battery_field_mask(BatteryField::Voltage) | battery_field_mask(BatteryField::Current) |
			battery_field_mask(BatteryField::Status) | battery_field_mask(BatteryField::TimeRemaining)), nullptr};
		flow.configure(gauge, 2000);
		sample = {};
		sample.set(BatteryField::Voltage, 4);
		sample.set(BatteryField::Current, -0.25f);
		sample.set_status(BatteryStatus::Discharging);
		sample.set(BatteryField::TimeRemaining, 0);
		flow.update(sample);
		assert(flow.snapshot().readings.get(BatteryField::TimeRemaining) == 0);
		sample = {};
		sample.set(BatteryField::Voltage, 4.1f);
		sample.set_status(BatteryStatus::Discharging);
		flow.update(sample);
		assert(!isfinite(flow.snapshot().readings.get(BatteryField::Power)));
		assert(!isfinite(flow.snapshot().readings.get(BatteryField::TimeRemaining)));
		return 0;
	}
#endif
	if (argc > 1 && strcmp(argv[1], "--duty-cycle") == 0) {
		failed = true;
		sensor_manager_sample_once();
		JsonDocument telemetry;
		JsonObject object = telemetry.to<JsonObject>();
		for (unsigned read = 0; read < 5; ++read) {
			now_ms += 10000;
			sensor_manager_sample_once();
			sensor_manager_append_api(object);
			assert(telemetry["temperature"].isNull());
			sensor_manager_append_mqtt(object);
			assert(telemetry["temperature"].isNull());
		}
		assert(fast_samples == 1 && slow_samples == 1 && event_samples == 1);
		return 0;
	}
	sensor_binding_init();
	sensor_binding_init();
	assert(binding_template_scheme_count() == 1);
	assert(binding_template_scheme_spec(0)->key_count() == 2);
#if HAS_BATTERY_INFO
	battery_binding_init();
#endif
	char unavailable[64];
	binding_template_resolve("[battery:percentage|N/A]", unavailable, sizeof(unavailable));
#if HAS_BATTERY_INFO
	assert(strcmp(unavailable, "N/A") == 0);
#else
	assert(strcmp(unavailable, "ERR:unknown") == 0);
	assert(!binding_template_scheme_known("battery", 7));
#endif
	expr_binding_init();
	char resolved[64] = {};
	binding_template_resolve("[sensor:temperature;%.1f|N/A]", resolved, sizeof(resolved));
	assert(strcmp(resolved, "N/A") == 0);
	double value = 0;
	assert(!sensor_manager_get_number("temperature", &value));
	assert(sensor_manager_field_count() == 3);
	sensor_manager_sample_once();
	sensor_manager_sample_once();
	assert(fast_samples == 1 && slow_samples == 1 && event_samples == 1);
	assert(sensor_manager_get_number("temperature", &value) && value == 24.5);
	binding_template_resolve("Temperature: [sensor:temperature;%.1f]", resolved, sizeof(resolved));
	assert(strcmp(resolved, "Temperature: 24.5") == 0);
	binding_template_resolve("[sensor:temperature;%n]", resolved, sizeof(resolved));
	assert(strcmp(resolved, "24.5") == 0);
	binding_template_resolve("[sensor:temperature;%.999999999f]", resolved, sizeof(resolved));
	assert(strcmp(resolved, "24.5") == 0);
	binding_template_resolve("[sensor:temperature;%f%f]", resolved, sizeof(resolved));
	assert(strcmp(resolved, "24.5") == 0);
	assert(binding_template_resolve_registered("sensor", 6, "unknown", resolved, sizeof(resolved)) == BINDING_RESOLVER_UNKNOWN);
	JsonDocument api;
	JsonObject api_object = api.to<JsonObject>();
	sensor_manager_append_api(api_object);
	assert(api["temperature"].as<double>() == value);
	JsonDocument mqtt;
	JsonObject mqtt_object = mqtt.to<JsonObject>();
	sensor_manager_append_mqtt(mqtt_object);
	assert(mqtt["temperature"].as<double>() == value);
	assert(!mqtt.containsKey("presence"));
	assert(fast_samples == 1 && slow_samples == 1);
	now_ms = 1999;
	sensor_manager_loop();
	assert(fast_samples == 1 && slow_samples == 1);
	assert(event_samples == 1);
	now_ms = 2000;
	event_state = true;
	sensor_manager_loop();
	bool boolean = false;
	assert(sensor_manager_get_number("presence", &value, &boolean) && boolean && value == 1);
	binding_template_resolve("[sensor:presence]", resolved, sizeof(resolved));
	assert(strcmp(resolved, "1") == 0);
	binding_template_resolve("[expr:[sensor:temperature] > 20 ? \"Hot\" : \"OK\"]", resolved, sizeof(resolved));
	assert(strcmp(resolved, "Hot") == 0);
	assert(fast_samples == 2 && slow_samples == 1);
	failed = true;
	now_ms = 10000;
	sensor_manager_loop();
	assert(slow_samples == 2);
	assert(sensor_manager_get_number("temperature", &value) && value == 24.5);
	sensor_manager_append_api(api_object);
	sensor_manager_append_mqtt(mqtt_object);
	assert(api["temperature"].as<double>() == 24.5 && mqtt["temperature"].as<double>() == 24.5);
	ota_active = true;
	event_state = false;
	now_ms = 20000;
	sensor_manager_loop();
	assert(slow_samples == 2 && fast_samples == 3);
	assert(sensor_manager_get_number("presence", &value) && value == 1);
	assert(!sensor_manager_get_number("unknown", &value));
	now_ms = 0xfffffff0UL;
	ota_active = false;
	sensor_manager_loop();
	assert(sensor_manager_get_number("presence", &value) && value == 0);
	const unsigned before_wrap = fast_samples;
	now_ms = 1984;
	sensor_manager_loop();
	assert(fast_samples == before_wrap + 1);
	return 0;
}
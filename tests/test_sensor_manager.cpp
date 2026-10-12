#define BOARD_CONFIG_H
#define HAS_MQTT 0
#define HAS_DISPLAY 1
#include "log_manager.h"
#include "../src/app/sensors/sensor_manager.cpp"
#include "../src/app/sensor_binding.cpp"
#include "../src/app/binding_template.cpp"
#undef TAG
#include "../src/app/expr_binding.cpp"
#include "../src/app/expr_eval.cpp"
#include <cassert>
#include <limits>

static unsigned long now_ms = 0;
static unsigned fast_samples = 0;
static unsigned slow_samples = 0;
static unsigned event_samples = 0;
static bool failed = false;
static bool event_state = false;
static bool ota_active = false;
unsigned long millis() { return now_ms; }
bool ota_activity_is_active() { return ota_active; }

void sensor_manager_register_all(SensorRegistry &registry) {
	static const SensorField fields[] = {{"temperature", "Temperature in Celsius."}};
	SensorCallbacks fast = {};
	fast.name = "fast";
	fast.fields = fields;
	fast.field_count = 1;
	fast.sample = [](JsonObject &doc) {
		++fast_samples;
		sensor_manager_set_number(doc, "temperature", 12.5f, !failed);
	};
	assert(registry.add(fast));
	SensorCallbacks slow = fast;
	slow.name = "slow";
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
	sensor_manager_init();
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
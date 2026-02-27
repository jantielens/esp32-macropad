#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "board_config.h"
#include <Arduino.h>
#include <ArduinoJson.h>

#if HAS_MQTT
class MqttManager;
#endif

struct SensorCallbacks {
	const char *name;
	void (*init)();
	// Optional per-loop handler for ISR-deferred work (e.g., event publishing).
	void (*loop)();
	void (*append_api)(JsonObject &doc);
	void (*append_mqtt)(JsonObject &doc);
#if HAS_MQTT
	void (*publish_ha)(MqttManager &mqtt);
#endif
};


class SensorRegistry {
public:
	bool add(const SensorCallbacks &callbacks);
};

// Implemented in sensors.cpp to register all enabled sensors.
void sensor_manager_register_all(SensorRegistry &registry);

// Sensor manager lifecycle
void sensor_manager_init();

// Optional: per-loop handler for event-driven sensors (safe, non-ISR context).
void sensor_manager_loop();

// Append sensor readings into API or MQTT JSON payloads.
void sensor_manager_append_api(JsonObject &doc);
void sensor_manager_append_mqtt(JsonObject &doc);

// Small JSON helpers to reduce per-sensor boilerplate.
// These write either a value or null based on a `valid` flag.
void sensor_manager_set_number(JsonObject &doc, const char *key, float value, bool valid);
void sensor_manager_set_bool(JsonObject &doc, const char *key, bool value, bool valid);

#if HAS_MQTT
void sensor_manager_publish_ha_discovery(MqttManager &mqtt);

// Build a full MQTT topic under the device base topic (e.g. "presence/state").
bool sensor_manager_build_state_topic(const char *state_topic_suffix, char *out_topic, size_t out_len);

// Publish binary state to a device-relative topic (payload "ON"/"OFF").
bool sensor_manager_publish_binary_state(const char *state_topic_suffix, bool on, bool retained);
#endif

#endif // SENSOR_MANAGER_H

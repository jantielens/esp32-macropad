#ifndef HA_DISCOVERY_H
#define HA_DISCOVERY_H

#include <Arduino.h>
#include "board_config.h"

#if HAS_MQTT

class MqttManager;

// Publish Home Assistant MQTT discovery configuration for the health sensors.
// Intended to be called once per boot after MQTT connects.
void ha_discovery_publish_health(MqttManager &mqtt);

// Shared helpers for sensor adapters to publish their own HA discovery entries.
bool ha_discovery_publish_sensor_config(
	MqttManager &mqtt,
	const char *object_id,
	const char *name_suffix,
	const char *value_template,
	const char *unit_of_measurement,
	const char *device_class,
	const char *state_class,
	const char *entity_category = nullptr
);

bool ha_discovery_publish_binary_sensor_config(
	MqttManager &mqtt,
	const char *object_id,
	const char *name_suffix,
	const char *value_template,
	const char *device_class,
	const char *entity_category,
	const char *state_topic = nullptr
);

// Variant for direct ON/OFF state topics (no value_template, uses "~/<suffix>").
bool ha_discovery_publish_binary_sensor_config_with_topic_suffix(
	MqttManager &mqtt,
	const char *object_id,
	const char *name_suffix,
	const char *state_topic_suffix,
	const char *device_class,
	const char *entity_category
);

#endif // HAS_MQTT

#endif // HA_DISCOVERY_H

#ifndef POWER_CONFIG_H
#define POWER_CONFIG_H

#include "config_manager.h"

enum class PowerMode {
		AlwaysOn,
		DutyCycle,
		Config,
		Ap
};

enum class PublishTransport {
		Ble,
		Mqtt,
		BleMqtt
};

enum class MqttPublishScope {
		SensorsOnly,
		DiagnosticsOnly,
		All
};

PowerMode power_config_parse_power_mode(const DeviceConfig *config);
PublishTransport power_config_parse_publish_transport(const DeviceConfig *config);
MqttPublishScope power_config_parse_mqtt_publish_scope(const DeviceConfig *config);

bool power_config_transport_includes_ble(PublishTransport transport);
bool power_config_transport_includes_mqtt(PublishTransport transport);

const char *power_config_power_mode_to_string(PowerMode mode);
const char *power_config_transport_to_string(PublishTransport transport);
const char *power_config_mqtt_scope_to_string(MqttPublishScope scope);

#endif // POWER_CONFIG_H

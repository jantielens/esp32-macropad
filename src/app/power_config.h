#ifndef POWER_CONFIG_H
#define POWER_CONFIG_H

#include "config_manager.h"

enum class PowerMode {
		AlwaysOn,
		DutyCycle,
		Config,
		Ap
};

enum class MqttPublishScope {
		SensorsOnly,
		DiagnosticsOnly,
		All
};

PowerMode power_config_parse_power_mode(const DeviceConfig *config);
MqttPublishScope power_config_parse_mqtt_publish_scope(const DeviceConfig *config);

const char *power_config_power_mode_to_string(PowerMode mode);
const char *power_config_mqtt_scope_to_string(MqttPublishScope scope);

#endif // POWER_CONFIG_H

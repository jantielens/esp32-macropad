#include "power_config.h"

#include <string.h>

static bool equals_ignore_case(const char *a, const char *b) {
		if (!a || !b) return false;
		while (*a && *b) {
				char ca = *a;
				char cb = *b;
				if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
				if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
				if (ca != cb) return false;
				a++;
				b++;
		}
		return *a == 0 && *b == 0;
}

PowerMode power_config_parse_power_mode(const DeviceConfig *config) {
		if (!config || strlen(config->power_mode) == 0) return PowerMode::AlwaysOn;

		if (equals_ignore_case(config->power_mode, "always_on")) return PowerMode::AlwaysOn;
		if (equals_ignore_case(config->power_mode, "duty_cycle")) return PowerMode::DutyCycle;
		if (equals_ignore_case(config->power_mode, "config")) return PowerMode::Config;
		if (equals_ignore_case(config->power_mode, "ap")) return PowerMode::Ap;

		return PowerMode::AlwaysOn;
}

PublishTransport power_config_parse_publish_transport(const DeviceConfig *config) {
		if (!config || strlen(config->publish_transport) == 0) return PublishTransport::Ble;

		if (equals_ignore_case(config->publish_transport, "ble")) return PublishTransport::Ble;
		if (equals_ignore_case(config->publish_transport, "mqtt")) return PublishTransport::Mqtt;
		if (equals_ignore_case(config->publish_transport, "ble_mqtt") || equals_ignore_case(config->publish_transport, "mqtt_ble")) {
				return PublishTransport::BleMqtt;
		}

		return PublishTransport::Ble;
}

MqttPublishScope power_config_parse_mqtt_publish_scope(const DeviceConfig *config) {
		if (!config || strlen(config->mqtt_publish_scope) == 0) return MqttPublishScope::SensorsOnly;

		if (equals_ignore_case(config->mqtt_publish_scope, "sensors_only")) return MqttPublishScope::SensorsOnly;
		if (equals_ignore_case(config->mqtt_publish_scope, "diagnostics_only")) return MqttPublishScope::DiagnosticsOnly;
		if (equals_ignore_case(config->mqtt_publish_scope, "all")) return MqttPublishScope::All;

		return MqttPublishScope::SensorsOnly;
}

bool power_config_transport_includes_ble(PublishTransport transport) {
		return transport == PublishTransport::Ble || transport == PublishTransport::BleMqtt;
}

bool power_config_transport_includes_mqtt(PublishTransport transport) {
		return transport == PublishTransport::Mqtt || transport == PublishTransport::BleMqtt;
}

const char *power_config_power_mode_to_string(PowerMode mode) {
		switch (mode) {
				case PowerMode::AlwaysOn: return "always_on";
				case PowerMode::DutyCycle: return "duty_cycle";
				case PowerMode::Config: return "config";
				case PowerMode::Ap: return "ap";
				default: return "always_on";
		}
}

const char *power_config_transport_to_string(PublishTransport transport) {
		switch (transport) {
				case PublishTransport::Ble: return "ble";
				case PublishTransport::Mqtt: return "mqtt";
				case PublishTransport::BleMqtt: return "ble_mqtt";
				default: return "ble";
		}
}

const char *power_config_mqtt_scope_to_string(MqttPublishScope scope) {
		switch (scope) {
				case MqttPublishScope::SensorsOnly: return "sensors_only";
				case MqttPublishScope::DiagnosticsOnly: return "diagnostics_only";
				case MqttPublishScope::All: return "all";
				default: return "sensors_only";
		}
}

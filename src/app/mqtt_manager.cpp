#include "mqtt_manager.h"

#include "board_config.h"

#if HAS_MQTT

#include "ha_discovery.h"
#include "device_telemetry.h"
#include "mqtt_sub_store.h"
#include "mqtt_screen.h"
#include "mqtt_audio.h"
#include "power_manager.h"
#include "power_config.h"
#include "log_manager.h"

MqttManager::MqttManager() : _client(_net) {}

void MqttManager::begin(const DeviceConfig *config, const char *friendly_name, const char *sanitized_name) {
		_config = config;

		if (friendly_name) {
				strlcpy(_friendly_name, friendly_name, sizeof(_friendly_name));
		}
		if (sanitized_name) {
				strlcpy(_sanitized_name, sanitized_name, sizeof(_sanitized_name));
		}

		// Safety: if sanitization produced an empty string, fall back to a stable default.
		if (strlen(_sanitized_name) == 0) {
				strlcpy(_sanitized_name, "esp32", sizeof(_sanitized_name));
		}

		snprintf(_base_topic, sizeof(_base_topic), "devices/%s", _sanitized_name);
		snprintf(_availability_topic, sizeof(_availability_topic), "%s/availability", _base_topic);
		snprintf(_health_state_topic, sizeof(_health_state_topic), "%s/health/state", _base_topic);

		_client.setBufferSize(MQTT_MAX_PACKET_SIZE);
		installCallback();

		_discovery_published_this_boot = false;
		_last_reconnect_attempt_ms = 0;
		_last_health_publish_ms = 0;
}

bool MqttManager::connectAndPublishDiscoveryBlocking(uint32_t timeout_ms) {
		if (!connectEnabled()) return false;
		if (_discovery_published_this_boot) return true;

		LOGI("MQTT", "Boot discovery: connecting to %s:%d (timeout %ums)",
				_config->mqtt_host, resolvedPort(), (unsigned)timeout_ms);

		unsigned long start = millis();
		bool conn = false;

		// PubSubClient::connect() blocks internally with its own socket timeout.
		// We retry in a loop until our overall deadline.
		while (!conn && (millis() - start) < timeout_ms) {
				conn = attemptConnectWithLWT(true);
				if (!conn) {
						LOGW("MQTT", "Boot connect attempt failed (state %d), retrying...", _client.state());
						delay(500);
				}
		}

		if (!conn) {
				LOGW("MQTT", "Boot discovery: broker unreachable after %ums - skipping", (unsigned)timeout_ms);
				return false;
		}

		LOGI("MQTT", "Boot discovery: connected");
		onConnected(true);

		// Let the broker process all retained messages before returning.
		_client.loop();
		delay(50);
		_client.loop();

		LOGI("MQTT", "Boot discovery: done (%ums)", (unsigned)(millis() - start));
		return true;
}

bool MqttManager::connectEnabled() const {
		if (!_config) return false;
		if (strlen(_config->mqtt_host) == 0) return false;
		return true;
}

uint16_t MqttManager::resolvedPort() const {
		if (!_config) return 1883;
		return _config->mqtt_port > 0 ? _config->mqtt_port : 1883;
}

bool MqttManager::enabled() const {
		// Enabled = we should connect to the broker.
		return connectEnabled();
}

bool MqttManager::publishEnabled() const {
		// Publishing health periodically is optional.
		if (!_config) return false;
		if (!connectEnabled()) return false;
		return _config->cycle_interval_seconds > 0;
}

bool MqttManager::connected() {
		return _client.connected();
}

bool MqttManager::publish(const char *topic, const char *payload, bool retained) {
		if (!enabled() || !_client.connected()) return false;
		if (!topic || !payload) return false;

		return _client.publish(topic, payload, retained);
}

bool MqttManager::publishJson(const char *topic, JsonDocument &doc, bool retained) {
		if (!topic) return false;

		// Avoid heap allocations inside String by using a bounded buffer.
		char payload[MQTT_MAX_PACKET_SIZE];
		size_t n = serializeJson(doc, payload, sizeof(payload));
		if (n == 0 || n >= sizeof(payload)) {
				LOGE("MQTT", "JSON payload too large for MQTT_MAX_PACKET_SIZE (%u)", (unsigned)sizeof(payload));
				return false;
		}

		if (!enabled() || !_client.connected()) return false;
		return _client.publish(topic, (const uint8_t*)payload, (unsigned)n, retained);
}

bool MqttManager::publishImmediate(const char *topic, const char *payload, bool retained) {
		return publish(topic, payload, retained);
}

bool MqttManager::subscribe(const char *topic) {
		if (!topic || !topic[0]) return false;
		if (!_client.connected()) return false;
		bool ok = _client.subscribe(topic);
		if (ok) {
				LOGI("MQTT", "Subscribed: %s", topic);
		} else {
				LOGW("MQTT", "Subscribe failed: %s", topic);
		}
		return ok;
}

void MqttManager::installCallback() {
		_client.setCallback([](char* topic, uint8_t* payload, unsigned int length) {
				mqtt_screen_on_message(topic, payload, length);
				mqtt_audio_on_message(topic, payload, length);
				mqtt_sub_store_set(topic, payload, length);
		});
}

void MqttManager::publishAvailability(bool online) {
		if (!_client.connected()) return;
		_client.publish(_availability_topic, online ? "online" : "offline", true);
}

void MqttManager::publishDiscoveryOncePerBoot() {
		if (_discovery_published_this_boot) return;

		LOGI("MQTT", "Publishing HA discovery");
		ha_discovery_publish_health(*this);
		_discovery_published_this_boot = true;
}

void MqttManager::publishHealthNow() {
		if (!_client.connected()) return;

		StaticJsonDocument<768> doc;
		const MqttPublishScope scope = power_config_parse_mqtt_publish_scope(_config);
		device_telemetry_fill_mqtt_scoped(doc, scope);

		if (doc.overflowed()) {
				LOGE("MQTT", "Health JSON overflow (StaticJsonDocument too small)");
				return;
		}

		char payload[MQTT_MAX_PACKET_SIZE];
		size_t n = serializeJson(doc, payload, sizeof(payload));
		if (n == 0 || n >= sizeof(payload)) {
				LOGE("MQTT", "Health JSON payload too large for MQTT_MAX_PACKET_SIZE (%u)", (unsigned)sizeof(payload));
				return;
		}
		if (_client.publish(_health_state_topic, (const uint8_t*)payload, (unsigned)n, true)) {
				LOGI("MQTT", "Published health (retained)");
		}
}

void MqttManager::publishHealthIfDue() {
		if (!_client.connected()) return;
		if (!publishEnabled()) return;

		unsigned long now = millis();
		unsigned long interval_ms = (unsigned long)_config->cycle_interval_seconds * 1000UL;

		if (_last_health_publish_ms == 0 || (now - _last_health_publish_ms) >= interval_ms) {
				StaticJsonDocument<768> doc;
				const MqttPublishScope scope = power_config_parse_mqtt_publish_scope(_config);
				device_telemetry_fill_mqtt_scoped(doc, scope);

				if (doc.overflowed()) {
						LOGE("MQTT", "Health JSON overflow (StaticJsonDocument too small)");
						return;
				}

				char payload[MQTT_MAX_PACKET_SIZE];
				size_t n = serializeJson(doc, payload, sizeof(payload));
				if (n == 0 || n >= sizeof(payload)) {
						LOGE("MQTT", "Health JSON payload too large for MQTT_MAX_PACKET_SIZE (%u)", (unsigned)sizeof(payload));
						return;
				}

				bool ok = _client.publish(_health_state_topic, (const uint8_t*)payload, (unsigned)n, true);

				if (ok) {
						_last_health_publish_ms = now;
						LOGI("MQTT", "Published health (retained)");
				}
		}
}

bool MqttManager::attemptConnectWithLWT(bool use_lwt) {
		_client.setServer(_config->mqtt_host, resolvedPort());

		char client_id[96];
		snprintf(client_id, sizeof(client_id), "%s", _sanitized_name);

		bool has_user = strlen(_config->mqtt_username) > 0;
		bool has_pass = strlen(_config->mqtt_password) > 0;

		if (has_user) {
				const char *pass = has_pass ? _config->mqtt_password : "";
				if (use_lwt) {
						return _client.connect(client_id, _config->mqtt_username, pass,
								_availability_topic, 0, true, "offline");
				} else {
						return _client.connect(client_id, _config->mqtt_username, pass);
				}
		} else {
				if (use_lwt) {
						return _client.connect(client_id,
								_availability_topic, 0, true, "offline");
				} else {
						return _client.connect(client_id);
				}
		}
}

void MqttManager::onConnected(bool publish_availability) {
		if (publish_availability) {
				publishAvailability(true);
		}

		if (power_manager_should_publish_mqtt_discovery()) {
				publishDiscoveryOncePerBoot();
		}

		// Re-subscribe to all tracked subscription topics.
		mqtt_sub_store_subscribe_all();

		// Screen control subscribe + initial state publish.
		mqtt_screen_on_connected();

		// Audio control subscribe + initial state publish.
		mqtt_audio_on_connected();

		// Publish a single retained state after connect so HA entities have values,
		// even when periodic publishing is disabled (interval = 0).
		publishHealthNow();

		// Start/reset interval timing for periodic health publishes.
		_last_health_publish_ms = millis();
}

void MqttManager::ensureConnected() {
		if (!enabled()) return;
		if (WiFi.status() != WL_CONNECTED) return;

		if (_client.connected()) return;

		unsigned long now = millis();
		if (_last_reconnect_attempt_ms > 0 && (now - _last_reconnect_attempt_ms) < 5000) {
				return;
		}
		_last_reconnect_attempt_ms = now;

		const bool duty_cycle = (power_manager_get_current_mode() == PowerMode::DutyCycle);

		LOGI("MQTT", "Connecting to %s:%d", _config->mqtt_host, resolvedPort());

		if (attemptConnectWithLWT(!duty_cycle)) {
				LOGI("MQTT", "Connected");
				onConnected(!duty_cycle);
		} else {
				LOGW("MQTT", "Connect failed (state %d)", _client.state());
		}
}

void MqttManager::loop() {
		if (!enabled()) return;

		ensureConnected();

		if (_client.connected()) {
				_client.loop();
				publishHealthIfDue();
		}
}

void MqttManager::disconnect() {
		if (_client.connected()) {
				_client.disconnect();
		}
}

#endif // HAS_MQTT

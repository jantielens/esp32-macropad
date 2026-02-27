#include "ld2410_out_sensor.h"

#if HAS_SENSOR_LD2410_OUT

#include "log_manager.h"
#include "sensor_manager.h"
#include "ha_discovery.h"
#if HAS_MQTT
#include "mqtt_manager.h"
#endif

static Ld2410OutSensor g_ld2410_out;

#if HAS_MQTT
static constexpr const char *kPresenceStateTopicSuffix = "presence/state";
#endif

static void IRAM_ATTR ld2410_out_isr() {
		// ISR should be minimal; just flag state changes.
		g_ld2410_out.handleChangeFromISR();
}

bool Ld2410OutSensor::begin() {
		if (_initialized) return _available;
		_initialized = true;

		if (LD2410_OUT_PIN < 0) {
				_available = false;
				LOGW("Sensor", "LD2410 OUT pin not configured");
				return false;
		}

		// OUT pin is a digital presence signal.
		pinMode(LD2410_OUT_PIN, INPUT_PULLDOWN);
		_presence = digitalRead(LD2410_OUT_PIN) == HIGH;
		_available = true;

		_pending_publish = true;
		_pending_presence = _presence;

		attachInterrupt(digitalPinToInterrupt(LD2410_OUT_PIN), ld2410_out_isr, CHANGE);
		LOGI("Sensor", "LD2410 OUT ready on GPIO%d", LD2410_OUT_PIN);
		return true;
}

void Ld2410OutSensor::handleChangeFromISR() {
		const unsigned long now = millis();
		if (_last_isr_ms != 0 && (now - _last_isr_ms) < (unsigned long)LD2410_OUT_DEBOUNCE_MS) {
				return;
		}
		_last_isr_ms = now;

		// ISR only updates volatile state; publishing must happen in normal context.
		_presence = (digitalRead(LD2410_OUT_PIN) == HIGH);
		_changed = true;
}

void Ld2410OutSensor::consumeChangeLog(bool presence) {
		LOGI("Sensor", "LD2410 presence: %s", presence ? "true" : "false");
}

void Ld2410OutSensor::loop() {
		if (!_available) return;

		bool changed = false;
		bool presence = false;

		// Snapshot ISR-updated state atomically.
		noInterrupts();
		if (_changed) {
				changed = true;
				presence = _presence;
				_changed = false;
		}
		interrupts();

		if (changed) {
				_pending_publish = true;
				_pending_presence = presence;
				consumeChangeLog(presence);
		}

#if HAS_MQTT
		// Publish immediately when MQTT is connected (no periodic batching).
		if (_pending_publish && sensor_manager_publish_binary_state(kPresenceStateTopicSuffix, _pending_presence, true)) {
				_pending_publish = false;
		}
#endif
}

void Ld2410OutSensor::appendJson(JsonObject &doc) {
		// Expose presence in /api/health.sensors (null if unavailable).
		sensor_manager_set_bool(doc, "presence", _presence, _available);
}

static void ld2410_out_init() {
		g_ld2410_out.begin();
}

static void ld2410_out_append_api(JsonObject &doc) {
		g_ld2410_out.appendJson(doc);
}

static void ld2410_out_loop() {
		g_ld2410_out.loop();
}

#if HAS_MQTT
static void ld2410_out_publish_ha(MqttManager &mqtt) {
		// Use a direct ON/OFF topic (no JSON template needed).
		ha_discovery_publish_binary_sensor_config_with_topic_suffix(
				mqtt,
				"presence",
				"Presence",
				kPresenceStateTopicSuffix,
				"presence",
				nullptr
		);
}
#endif

void register_ld2410_out_sensor(SensorRegistry &registry) {
		SensorCallbacks callbacks = {};
		callbacks.name = "LD2410_OUT";
		callbacks.init = ld2410_out_init;
		callbacks.loop = ld2410_out_loop;
		callbacks.append_api = ld2410_out_append_api;
#if HAS_MQTT
		callbacks.publish_ha = ld2410_out_publish_ha;
#endif
		registry.add(callbacks);
}

#endif // HAS_SENSOR_LD2410_OUT

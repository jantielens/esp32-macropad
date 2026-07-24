#include "device_class.h"

#include "log_manager.h"

static const DeviceClass *g_classes[MAX_DEVICE_CLASSES] = { nullptr };
static unsigned g_count = 0;

bool device_class_register(const DeviceClass *cls) {
		if (!cls) return false;
		if (g_count >= MAX_DEVICE_CLASSES) {
				LOGE("DevClass", "Registry full (MAX_DEVICE_CLASSES=%u); rejected '%s'",
					 (unsigned)MAX_DEVICE_CLASSES, cls->name ? cls->name : "?");
				return false;
		}
		g_classes[g_count++] = cls;
		LOGI("DevClass", "Registered '%s' (owned_mode=%d)",
			 cls->name ? cls->name : "?", (int)cls->owned_mode);
		return true;
}

unsigned device_class_count() {
		return g_count;
}

const DeviceClass *device_class_get(unsigned index) {
		if (index >= g_count) return nullptr;
		return g_classes[index];
}

const DeviceClass *device_class_find_by_mode(PowerMode mode) {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->owned_mode == mode) return c;
		}
		return nullptr;
}

// Lifecycle -------------------------------------------------------------------
void device_class_dispatch_setup_early(DeviceConfig *config, PowerMode boot_mode) {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->on_setup_early) c->on_setup_early(config, boot_mode);
		}
}

void device_class_dispatch_setup_late(DeviceConfig *config, PowerMode current_mode) {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->on_setup_late) c->on_setup_late(config, current_mode);
		}
}

void device_class_dispatch_loop() {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->on_loop) c->on_loop();
		}
}

// Power -----------------------------------------------------------------------
bool device_class_dispatch_wake_classify(bool *force_config) {
		bool any_handled = false;
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->on_wake_classify) {
						bool handled = false;
						c->on_wake_classify(&handled, force_config);
						if (handled) any_handled = true;
				}
		}
		return any_handled;
}

void device_class_dispatch_sleep_prepare(uint32_t *seconds_inout) {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->on_sleep_prepare) c->on_sleep_prepare(seconds_inout);
		}
}

// Config ----------------------------------------------------------------------
void device_class_dispatch_config_defaults(DeviceConfig *config) {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->config_defaults) c->config_defaults(config);
		}
}

void device_class_dispatch_config_load(DeviceConfig *config, Preferences &preferences) {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->config_load) c->config_load(config, preferences);
		}
}

void device_class_dispatch_config_save(const DeviceConfig *config, Preferences &preferences) {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->config_save) c->config_save(config, preferences);
		}
}

void device_class_dispatch_config_api_get(const DeviceConfig *config, JsonObject &root) {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->config_api_get) c->config_api_get(config, root);
		}
}

const char *device_class_dispatch_config_api_validate(const DeviceConfig *config, JsonObject &body) {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (!c || !c->config_api_validate) continue;
				const char *error = c->config_api_validate(config, body);
				if (error) return error;
		}
		return nullptr;
}

void device_class_dispatch_config_api_set(DeviceConfig *config, JsonObject &body) {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->config_api_set) c->config_api_set(config, body);
		}
}

// MQTT ------------------------------------------------------------------------
void device_class_dispatch_mqtt_discovery(MqttManager &mqtt, bool *skip_generic) {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->mqtt_on_discovery) c->mqtt_on_discovery(mqtt, skip_generic);
		}
}

void device_class_dispatch_mqtt_state(MqttManager &mqtt) {
		for (unsigned i = 0; i < g_count; ++i) {
				const DeviceClass *c = g_classes[i];
				if (c && c->mqtt_publish_state) c->mqtt_publish_state(mqtt);
		}
}

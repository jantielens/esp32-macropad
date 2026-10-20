#include "board_config.h"
#include "ha_binding.h"

#if HAS_HA_LIVE
#include "ha_live.h"
#include "binding_template.h"
#include "binding_value.h"
#include "log_manager.h"
#include <esp_heap_caps.h>
#include <string.h>

static char* ha_snapshot = nullptr; // LVGL task only.

static BindingResolverStatus ha_resolve(const char* params, char* out, size_t size) {
    char entity[HA_LIVE_ENTITY_BYTES], path[128], format[64];
    if (!binding_value_parse_params(params, entity, sizeof(entity), path, sizeof(path),
                                    format, sizeof(format)) || !ha_live_valid_entity(entity))
        return BINDING_RESOLVER_UNKNOWN;
    HaLiveStatus status{};
    ha_live_status(status);
    if (!status.configured) return BINDING_RESOLVER_UNAVAILABLE;
    if (!ha_snapshot) {
        ha_snapshot = static_cast<char*>(heap_caps_malloc(HA_LIVE_SNAPSHOT_BYTES,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!ha_snapshot) {
            LOGE("HABind", "PSRAM snapshot allocation failed");
            return BINDING_RESOLVER_UNAVAILABLE;
        }
    }
    if (!ha_live_read(entity, ha_snapshot, HA_LIVE_SNAPSHOT_BYTES))
        return BINDING_RESOLVER_UNAVAILABLE;
    char value[BINDING_TEMPLATE_MAX_LEN];
    const char* key = !path[0] || strcmp(path, ".") == 0 ? "state" : path;
    if (!binding_value_extract_json(ha_snapshot, key, value, sizeof(value)))
        return BINDING_RESOLVER_UNAVAILABLE;
    binding_value_format(value, format, out, size);
    return BINDING_RESOLVER_RESOLVED;
}

static const char* ha_binding_status() {
    HaLiveStatus status{};
    ha_live_status(status);
    return status.connected ? nullptr : status.last_error[0] ? status.last_error : status.state;
}

static const BindingParamDoc params[] = {
    {"entity", "Home Assistant entity ID, such as sensor.solar_power."},
    {"path", "Empty or . for state; state or a dotted path such as attributes.brightness."},
    {"format", "printf format such as %.1f, %d, or %s; units may be included."},
};
static const BindingExampleDoc examples[] = {
    {"[ha:sensor.solar_power]", "Live entity state without MQTT."},
    {"[ha:sensor.solar_power;;%.2f kW]", "Format a numeric state."},
    {"[ha:light.kitchen;attributes.brightness;%d]", "Read an entity attribute."},
    {"[ha:sensor.solar_power|Offline]", "Fallback while disconnected or unavailable."},
};
static const BindingSchemeDoc kHaDoc = {
    "Data", "Live Home Assistant states and attributes using the configured HA URL and token.",
    BINDING_DOC_LIST(params), BINDING_DOC_NONE, BINDING_DOC_LIST(examples),
    nullptr, false, BINDING_DOC_NONE,
    "Local HTTP only. Unknown/unavailable entities use the fallback. Saved pads and temporary previews subscribe automatically.",
    ha_binding_status,
};

void ha_binding_init() {
    if (!binding_template_register("ha", ha_resolve, nullptr,
            {1, 3, 2, 2, BINDING_VALIDATION_STANDARD, true, nullptr, nullptr, &kHaDoc}))
        LOGE("HABind", "HA scheme registration failed");
}
#else
void ha_binding_init() {}
#endif

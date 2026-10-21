#include "sensor_binding.h"
#include "board_config.h"

#if HAS_DISPLAY

#include "binding_template.h"
#include "sensors/sensor_manager.h"
#include "log_manager.h"
#include <stdio.h>
#include <string.h>
#include <new>

static BindingKeyDoc *s_sensor_keys = nullptr;
static BindingAuthoringKey *s_sensor_authoring_keys = nullptr;
static BindingAuthoringSpec s_sensor_authoring = {};
static uint8_t s_sensor_key_count = 0;

static uint8_t sensor_binding_key_count() { return s_sensor_key_count; }

static const char *sensor_binding_key_at(uint8_t index) {
    return index < s_sensor_key_count ? s_sensor_keys[index].key : nullptr;
}

static bool sensor_format_valid(const char *format) {
    if (!format || *format != '%') return false;
    const char *cursor = format + 1;
    while (*cursor && strchr("-+ #0", *cursor)) ++cursor;
    unsigned width = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        width = width * 10 + (*cursor++ - '0');
        if (width > BINDING_TEMPLATE_MAX_LEN) return false;
    }
    if (*cursor == '.') {
        ++cursor;
        unsigned precision = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            precision = precision * 10 + (*cursor++ - '0');
            if (precision > BINDING_TEMPLATE_MAX_LEN) return false;
        }
    }
    return *cursor && strchr("fFeEgG", *cursor) && cursor[1] == '\0';
}

static BindingResolverStatus sensor_binding_resolve(const char *params, char *out, size_t out_len) {
    if (!params || !out || !out_len) return BINDING_RESOLVER_UNKNOWN;
    const char *separator = strchr(params, ';');
    const size_t key_len = separator ? (size_t)(separator - params) : strlen(params);
    const char *key = nullptr;
    for (uint8_t index = 0; index < s_sensor_key_count; ++index) {
        if (strlen(s_sensor_keys[index].key) == key_len &&
            strncmp(s_sensor_keys[index].key, params, key_len) == 0) {
            key = s_sensor_keys[index].key;
            break;
        }
    }
    if (!key) return BINDING_RESOLVER_UNKNOWN;
    double value = 0;
    if (!sensor_manager_get_number(key, &value)) return BINDING_RESOLVER_UNAVAILABLE;
    const char *format = separator ? separator + 1 : nullptr;
    snprintf(out, out_len, sensor_format_valid(format) ? format : "%.10g", value);
    return BINDING_RESOLVER_RESOLVED;
}

static const BindingParamDoc kSensorParams[] = {
    {"key", "A local sensor field registered by this board's firmware."},
    {"format", "Single printf float format, e.g. %.1f or %.0f; invalid formats use the default."},
};
static char s_sensor_example[128];
static const BindingExampleDoc kSensorExamples[] = {
    {s_sensor_example, "Local reading with one decimal place and an unavailable fallback."},
};

void sensor_binding_init() {
    if (s_sensor_keys) return;
    const size_t field_count = sensor_manager_field_count();
    if (!field_count) return;
    if (field_count > UINT8_MAX) {
        LOGE("SensorBind", "Too many sensor fields for binding metadata");
        return;
    }
    s_sensor_keys = new (std::nothrow) BindingKeyDoc[field_count];
    s_sensor_authoring_keys = new (std::nothrow) BindingAuthoringKey[field_count];
    if (!s_sensor_keys || !s_sensor_authoring_keys) {
        delete[] s_sensor_keys;
        delete[] s_sensor_authoring_keys;
        s_sensor_keys = nullptr;
        s_sensor_authoring_keys = nullptr;
        LOGE("SensorBind", "Sensor metadata allocation failed");
        return;
    }
    for (size_t field = 0; field < field_count; ++field) {
        const SensorField *definition = sensor_manager_field_at(field);
        uint8_t index = 0;
        while (index < s_sensor_key_count && strcmp(s_sensor_keys[index].key, definition->key) != 0) ++index;
        if (index == s_sensor_key_count) ++s_sensor_key_count;
        s_sensor_keys[index] = {definition->key, definition->summary, nullptr};
        s_sensor_authoring_keys[index] = {
            definition->key, BINDING_AUTHORING_KIND_NUMBER, definition->unit,
            BINDING_AUTHORING_FORMAT_RAW | BINDING_AUTHORING_FORMAT_NUMBER, nullptr,
            BINDING_AUTHORING_CAP_DISPLAY | BINDING_AUTHORING_CAP_NUMERIC_WIDGET |
                BINDING_AUTHORING_CAP_LIVE_SAMPLING,
            true, definition->example_value,
        };
    }
    s_sensor_authoring = {s_sensor_authoring_keys, s_sensor_key_count};
    snprintf(s_sensor_example, sizeof(s_sensor_example), "[sensor:%s;%%.1f|N/A]", s_sensor_keys[0].key);
    static const BindingSchemeDoc kSensorDoc = {
        "Device", "Latest local sensor readings, without an MQTT broker.",
        BINDING_DOC_LIST(kSensorParams), s_sensor_keys, s_sensor_key_count, BINDING_DOC_LIST(kSensorExamples),
        nullptr, false, BINDING_DOC_NONE,
        "Values retain the last successful sample until reboot. Booleans are 0 or 1. Later registered sensors win on duplicate keys.",
        nullptr,
    };
    if (!binding_template_register("sensor", sensor_binding_resolve, nullptr,
                                  {1, 2, 1, 1, BINDING_VALIDATION_STANDARD, false,
                                   sensor_binding_key_count, sensor_binding_key_at, &kSensorDoc,
                                   &s_sensor_authoring})) {
        LOGE("SensorBind", "Failed to register sensor binding scheme");
    }
}

#else

void sensor_binding_init() {}

#endif
#include "battery_binding.h"
#if HAS_BATTERY_INFO

#include "battery_info.h"
#include "binding_template.h"
#include "binding_value.h"
#include "config_psram.h"
#include "sensors/sensor_manager.h"
#include "log_manager.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
struct BatteryBindingMetadata {
    BindingKeyDoc keys[BATTERY_FIELD_COUNT];
    BindingAuthoringKey authoring_keys[BATTERY_FIELD_COUNT];
    BindingAuthoringSpec authoring;
    BindingSchemeDoc doc;
};
BatteryBindingMetadata *metadata = nullptr;

uint8_t key_count() { return BATTERY_FIELD_COUNT; }
const char *key_at(uint8_t index) {
    return index < BATTERY_FIELD_COUNT ? battery_field_info(static_cast<BatteryField>(index)).key : nullptr;
}

BindingResolverStatus resolve(const char *params, char *out, size_t out_len) {
    const char *separator = strchr(params, ';');
    const size_t length = separator ? static_cast<size_t>(separator - params) : strlen(params);
    for (size_t index = 0; index < BATTERY_FIELD_COUNT; ++index) {
        const BatteryField field = static_cast<BatteryField>(index);
        const char *key = battery_field_info(field).key;
        if (strlen(key) != length || strncmp(key, params, length)) continue;
        BatterySnapshot snapshot;
        sensor_manager_get_battery(snapshot);
        const float value = snapshot.readings.get(field);
        if (!(snapshot.supported & battery_field_mask(field)) || !isfinite(value))
            return BINDING_RESOLVER_UNAVAILABLE;
        char raw[32];
        if (field == BatteryField::Status) {
            const char *text = battery_status_text(value);
            if (!text) return BINDING_RESOLVER_UNAVAILABLE;
            snprintf(out, out_len, "%s", text);
        } else {
            snprintf(raw, sizeof(raw), "%.10g", static_cast<double>(value));
            if (separator && separator[1])
                binding_value_format(raw, separator + 1, out, out_len);
            else
                snprintf(out, out_len, "%s", raw);
        }
        return BINDING_RESOLVER_RESOLVED;
    }
    return BINDING_RESOLVER_UNKNOWN;
}

const char *provider_status() {
    BatterySnapshot snapshot;
    sensor_manager_get_battery(snapshot);
    return snapshot.supported ? nullptr : "This board has no battery telemetry provider.";
}
}

void battery_binding_init() {
    if (metadata) return;
    BatterySnapshot snapshot;
    sensor_manager_get_battery(snapshot);
    static const char note_prefix[] =
        "Unsupported or stale fields use the unavailable fallback, never an invented zero. "
        "Readings expire after three sampling intervals, excluding OTA pauses. "
        "Status and runtime estimates clear immediately when invalid or inapplicable. "
        "Numeric formatting uses printf; status is text. Units stay outside the token. ";
    const char *description = snapshot.description ? snapshot.description : "No battery measurements are available on this board.";
    const size_t note_size = sizeof(note_prefix) + strlen(description);
    void *storage = config_psram_alloc(sizeof(BatteryBindingMetadata) + note_size, "battery_binding");
    if (!storage) {
        LOGE("BatteryBind", "Failed to allocate battery binding metadata");
        return;
    }
    BatteryBindingMetadata *candidate = new (storage) BatteryBindingMetadata{};
    char *note = reinterpret_cast<char *>(candidate + 1);
    snprintf(note, note_size, "%s%s", note_prefix, description);
    for (size_t index = 0; index < BATTERY_FIELD_COUNT; ++index) {
        const BatteryField field = static_cast<BatteryField>(index);
        const BatteryFieldInfo &info = battery_field_info(field);
        candidate->keys[index] = {info.key, info.description,
            snapshot.supported & battery_field_mask(field) ? "Supported on this board" : "Unsupported on this board"};
        candidate->authoring_keys[index] = field == BatteryField::Status
            ? binding_authoring_text(info.key, info.example)
            : BindingAuthoringKey{info.key, BINDING_AUTHORING_KIND_NUMBER, info.unit,
                BINDING_AUTHORING_FORMAT_RAW | BINDING_AUTHORING_FORMAT_NUMBER, nullptr,
                BINDING_AUTHORING_CAP_DISPLAY | BINDING_AUTHORING_CAP_NUMERIC_WIDGET |
                    BINDING_AUTHORING_CAP_LIVE_SAMPLING, true, info.example};
    }
    static const BindingParamDoc params[] = {
        {"key", "A canonical battery field; unsupported fields remain valid bindings."},
        {"format", "Optional numeric printf format, e.g. %.2f; status is returned as text."},
    };
    static const BindingExampleDoc examples[] = {
        {"[battery:percentage;%.0f|N/A]", "Battery charge percentage, when available."},
        {"[battery:status|Unknown]", "Readable battery state without a conditional expression."},
        {"[battery:discharge_power;%.2f|N/A]", "Positive battery draw in watts, not total device power."},
        {"[battery:time_remaining;%.0f|N/A]", "Estimated remaining runtime in minutes while discharging."},
    };
    candidate->authoring = {candidate->authoring_keys, BATTERY_FIELD_COUNT};
    BindingSchemeDoc &kBatteryDoc = candidate->doc;
    kBatteryDoc = {
        "Device", "Board-independent battery telemetry; individual measurements depend on hardware.",
        BINDING_DOC_LIST(params), candidate->keys, BATTERY_FIELD_COUNT, BINDING_DOC_LIST(examples),
        nullptr, false, BINDING_DOC_NONE, note, provider_status,
    };
    if (!binding_template_register("battery", resolve, nullptr,
        {1, 2, 1, 1, BINDING_VALIDATION_STANDARD, false, key_count, key_at, &kBatteryDoc, &candidate->authoring})) {
        LOGE("BatteryBind", "Failed to register battery binding scheme");
        candidate->~BatteryBindingMetadata();
        free(storage);
        return;
    }
    metadata = candidate;
}

#endif

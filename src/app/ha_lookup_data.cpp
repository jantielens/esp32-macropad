#include "ha_lookup_data.h"
#if HAS_HA_LIVE
#include <stdio.h>
#include <string.h>

static size_t sample_length(const char* value, size_t limit) {
    size_t size = strnlen(value, limit + 1);
    if (size <= limit) return size;
    size = limit;
    while (size && (static_cast<unsigned char>(value[size]) & 0xc0) == 0x80) --size;
    return size;
}

static void add_paths(ArduinoJson::JsonVariantConst value, const char* path,
                      ArduinoJson::JsonArray paths, bool& more, unsigned depth = 0) {
    if (value.is<ArduinoJson::JsonObjectConst>() && depth < 4) {
        for (ArduinoJson::JsonPairConst pair : value.as<ArduinoJson::JsonObjectConst>()) {
            const char* key = pair.key().c_str();
            if (strpbrk(key, ".;[]|") || !key[0]) continue;
            char child[128];
            const int length = snprintf(child, sizeof(child), "%s.%s", path, key);
            if (length < 0 || size_t(length) >= sizeof(child)) { more = true; continue; }
            add_paths(pair.value(), child, paths, more, depth + 1);
        }
    } else if (value.is<ArduinoJson::JsonObjectConst>() || value.is<ArduinoJson::JsonArrayConst>()) {
        more = true;
    } else if (!value.isNull()) {
        if (paths.size() >= 32) { more = true; return; }
        ArduinoJson::JsonObject item = paths.add<ArduinoJson::JsonObject>();
        item["path"] = ArduinoJson::JsonString(path, ArduinoJson::JsonString::Copied);
        if (value.is<const char*>()) {
            const char* text = value.as<const char*>();
            item["value"] = ArduinoJson::JsonString(text, sample_length(text, 192), ArduinoJson::JsonString::Copied);
            item["truncated"] = strlen(text) > 192;
        } else {
            char sample[64];
            ArduinoJson::serializeJson(value, sample, sizeof(sample));
            item["value"] = sample;
        }
    }
}

const char* ha_lookup_describe_entity(ArduinoJson::JsonObjectConst state,
                                     const char* entity,
                                     ArduinoJson::JsonDocument& detail) {
    if (!state["state"].is<const char*>() || state["entity_id"] != entity)
        return "Home Assistant returned an unexpected entity response.";
    const char* raw = state["state"];
    detail["entity_id"] = ArduinoJson::JsonString(entity, ArduinoJson::JsonString::Copied);
    detail["state"] = ArduinoJson::JsonString(raw, sample_length(raw, 192), ArduinoJson::JsonString::Copied);
    detail["state_truncated"] = strlen(raw) > 192;
    const char* unit = state["attributes"]["unit_of_measurement"] | "";
    detail["unit"] = ArduinoJson::JsonString(unit, sample_length(unit, 32), ArduinoJson::JsonString::Copied);
    bool more = false;
    add_paths(state["attributes"], "attributes", detail["paths"].to<ArduinoJson::JsonArray>(), more);
    detail["more"] = more;
    return detail.overflowed() ? "HA entity detail JSON allocation failed." : nullptr;
}
#endif

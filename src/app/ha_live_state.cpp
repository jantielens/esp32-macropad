#include "board_config.h"
#if HAS_HA_LIVE
#include "ha_live_state.h"
#include <string.h>

const char* HaLiveMessage::append(const void* bytes, size_t length, bool start, bool more) {
    if (start) {
        if (fragmented) return "HA overlapping fragments";
        size = 0;
    } else if (!fragmented) return "HA unexpected fragment";
    if (length > capacity - size) return "HA message exceeds 15 KiB";
    if (length) memcpy(data + size, bytes, length);
    size += length;
    data[size] = '\0';
    fragmented = more;
    return nullptr;
}

bool ha_live_state_available(ArduinoJson::JsonVariantConst state) {
    const char* value = state["state"] | "";
    return value[0] && strcmp(value, "unknown") && strcmp(value, "unavailable");
}

const char* ha_live_apply_state(ArduinoJson::JsonDocument& state,
                               ArduinoJson::JsonObjectConst event,
                               const char* entity) {
    for (auto removed : event["r"].as<ArduinoJson::JsonArrayConst>()) {
        if (removed == entity) { state.clear(); return nullptr; }
    }
    auto added = event["a"][entity];
    if (!added.isUnbound()) {
        if (!added["s"].is<const char*>() || !added["a"].is<ArduinoJson::JsonObjectConst>())
            return "invalid HA entity snapshot";
        state.clear();
        state["entity_id"] = entity;
        state["state"] = added["s"];
        state["attributes"] = added["a"];
        state["context"] = added["c"];
        state["last_changed"] = added["lc"];
        state["last_updated"] = added["lu"];
    }
    auto changed = event["c"][entity];
    if (!changed.isUnbound()) {
        if (!state["state"].is<const char*>()) return "HA delta without initial state";
        auto plus = changed["+"];
        if (!plus["s"].isUnbound()) {
            if (!plus["s"].is<const char*>()) return "invalid HA state delta";
            state["state"] = plus["s"];
        }
        if (!plus["a"].isUnbound()) {
            if (!plus["a"].is<ArduinoJson::JsonObjectConst>()) return "invalid HA attribute delta";
            for (ArduinoJson::JsonPairConst pair : plus["a"].as<ArduinoJson::JsonObjectConst>())
                state["attributes"][pair.key()] = pair.value();
        }
        if (!plus["c"].isUnbound()) state["context"] = plus["c"];
        if (!plus["lc"].isUnbound()) state["last_changed"] = plus["lc"];
        if (!plus["lu"].isUnbound()) state["last_updated"] = plus["lu"];
        auto minus = changed["-"]["a"];
        if (!minus.isUnbound()) {
            if (!minus.is<ArduinoJson::JsonArrayConst>()) return "invalid HA attribute removals";
            for (auto key : minus.as<ArduinoJson::JsonArrayConst>()) {
                if (!key.is<const char*>()) return "invalid HA attribute removal key";
                state["attributes"].remove(key.as<const char*>());
            }
        }
    }
    if (state.overflowed()) return "HA JSON allocation budget exceeded";
    const auto value = state["state"];
    if (value.is<const char*>() &&
        strlen(value.as<const char*>()) != value.as<ArduinoJson::JsonString>().size())
        return "HA state contains embedded NUL";
    return nullptr;
}
#endif

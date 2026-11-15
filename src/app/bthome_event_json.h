#pragma once

#include "bthome_event_settings.h"
#include "json_integer.h"

namespace bthome {
inline bool event_settings_update_present(JsonObjectConst doc) {
    return !doc["bthome_event_burst_ms"].isUnbound() ||
        !doc["bthome_event_adv_interval_ms"].isUnbound();
}

inline const char* parse_event_settings_update(JsonObjectConst doc,
        const EventSettings& previous, EventSettings& candidate) {
    EventSettings settings = previous;
    int32_t value;
    if (!doc["bthome_event_burst_ms"].isUnbound()) {
        if (!parse_json_integer(doc["bthome_event_burst_ms"], value) || value < 100 || value > 1000)
            return "BTHome event burst duration must be an integer from 100 to 1000 ms";
        settings.burst_ms = static_cast<uint16_t>(value);
    }
    if (!doc["bthome_event_adv_interval_ms"].isUnbound()) {
        if (!parse_json_integer(doc["bthome_event_adv_interval_ms"], value) || value < 20 || value > 300)
            return "BTHome event advertising interval must be an integer from 20 to 300 ms";
        settings.adv_interval_ms = static_cast<uint16_t>(value);
    }
    if (!valid_event_settings(settings))
        return "BTHome event burst duration must be at least three advertising intervals";
    candidate = settings;
    return nullptr;
}
}

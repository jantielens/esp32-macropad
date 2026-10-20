#pragma once

#include <ArduinoJson.h>
#include <stddef.h>

// Returns nullptr on success. On error, state may be partially modified;
// the caller must discard it and invalidate the affected cached entity.
const char* ha_live_apply_state(ArduinoJson::JsonDocument& state,
                               ArduinoJson::JsonObjectConst event,
                               const char* entity);
bool ha_live_state_available(ArduinoJson::JsonVariantConst state);

struct HaLiveMessage {
    HaLiveMessage(char* buffer, size_t limit) : data(buffer), capacity(limit) {}
    char* data;
    size_t capacity; // Payload limit; data must provide capacity + 1 bytes for the NUL.
    size_t size = 0;
    bool fragmented = false;
    const char* append(const void* bytes, size_t length, bool start, bool more);
};

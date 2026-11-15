#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

inline bool parse_json_integer(JsonVariantConst field, int32_t& value) {
    if (field.is<int32_t>()) {
        value = field.as<int32_t>();
        return true;
    }
    if (!field.is<const char*>()) return false;
    const char* text = field.as<const char*>();
    if (!text || !*text) return false;
    const bool negative = *text == '-';
    if (negative) ++text;
    if (!*text) return false;
    uint32_t parsed = 0;
    const uint32_t limit = negative ? uint32_t(INT32_MAX) + 1u : uint32_t(INT32_MAX);
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        const uint32_t digit = *text - '0';
        if (parsed > (limit - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    value = negative ? int32_t(-int64_t(parsed)) : int32_t(parsed);
    return true;
}

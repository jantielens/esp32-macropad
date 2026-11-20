#pragma once

#include <ArduinoJson.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

inline bool microphone_parse_settings(JsonObjectConst object, int8_t* gain_db, bool* reduce_rumble) {
    int gain = *gain_db;
    bool rumble = *reduce_rumble;
    if (!object["microphone_gain_db"].isUnbound()) {
        JsonVariantConst field = object["microphone_gain_db"];
        if (field.is<int>()) gain = field.as<int>();
        else if (field.is<const char*>()) {
            JsonString value = field.as<JsonString>();
            const char* text = value.c_str();
            const char* digits = text && (*text == '-' || *text == '+') ? text + 1 : text;
            if (!text || strlen(text) != value.size() || !digits || !*digits ||
                strspn(digits, "0123456789") != strlen(digits)) return false;
            char* end = nullptr;
            errno = 0;
            const long parsed = strtol(text, &end, 10);
            if (errno == ERANGE || *end || parsed < -12 || parsed > 24) return false;
            gain = static_cast<int>(parsed);
        } else return false;
        if (gain < -12 || gain > 24) return false;
    }
    if (!object["microphone_reduce_rumble"].isUnbound()) {
        if (!object["microphone_reduce_rumble"].is<bool>()) return false;
        rumble = object["microphone_reduce_rumble"].as<bool>();
    }
    *gain_db = static_cast<int8_t>(gain);
    *reduce_rumble = rumble;
    return true;
}

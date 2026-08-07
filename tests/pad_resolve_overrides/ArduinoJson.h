#pragma once

#include <Arduino.h>
#include_next <ArduinoJson.h>

namespace ArduinoJson {
template <>
struct Converter<::String> {
    static void toJson(const ::String& source, JsonVariant destination) {
        destination.set(source.c_str());
    }
};
}
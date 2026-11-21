#pragma once

#include <Arduino.h>
#include_next <ArduinoJson.h>
#include <string>

namespace ArduinoJson {
template <>
struct Converter<::String> {
    static void toJson(const ::String& source, JsonVariant destination) {
        destination.set(std::string(source.c_str()));
    }
};
}
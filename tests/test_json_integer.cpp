#include "json_integer.h"

#include <cassert>

int main() {
    const struct {
        const char* json;
        int32_t value;
    } valid[] = {
        {"0", 0}, {"\"-0\"", 0}, {"65535", 65535}, {"\"65535\"", 65535},
        {"\"2147483647\"", INT32_MAX}, {"\"-2147483648\"", INT32_MIN}
    };
    for (const auto& entry : valid) {
        JsonDocument doc;
        assert(!deserializeJson(doc, entry.json));
        int32_t value = 0;
        assert(parse_json_integer(doc.as<JsonVariantConst>(), value));
        assert(value == entry.value);
    }
    for (const char* json : {"true", "null", "[]", "{}", "1.5", "\"\"", "\"-\"",
                            "\" 300\"", "\"+300\"", "\"3e2\"", "\"300s\"",
                            "\"2147483648\"", "\"-2147483649\"", "4294967296"}) {
        JsonDocument doc;
        assert(!deserializeJson(doc, json));
        int32_t value = 17;
        assert(!parse_json_integer(doc.as<JsonVariantConst>(), value));
        assert(value == 17);
    }
}

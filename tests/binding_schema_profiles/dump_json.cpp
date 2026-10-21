#include <cstdio>
#include <string>

#include <ArduinoJson.h>

#include "binding_schema.h"

// Prints the same payload as GET /api/bindings?docs=1.
void binding_schema_dump_json() {
    JsonDocument doc;
    JsonArray schemes = doc["schemes"].to<JsonArray>();
    binding_schema_emit(&schemes, true);
    std::string out;
    serializeJsonPretty(doc, out);
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '\r' && i + 1 < out.size() && out[i + 1] == '\n') continue;
        std::putchar(out[i]);
    }
    std::putchar('\n');
}

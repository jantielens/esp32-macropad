#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

#include "binding_template.h"
#include "pad_validate.h"
#include "widgets/widget_registry.h"

static int g_failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::printf("FAIL: %s (line %d)\n", #condition, __LINE__); \
        g_failures++; \
    } \
} while (0)

const WidgetType* widget_find(const char* type_name) {
    static WidgetType test_widget = {};
    if (type_name && std::strcmp(type_name, "test") == 0) {
        test_widget.name = "test";
        return &test_widget;
    }
    return nullptr;
}

bool binding_template_scheme_known(const char* scheme, size_t name_len) {
    (void)scheme;
    (void)name_len;
    return true;
}

const char* binding_template_validate_params(const char* scheme, size_t name_len,
                                             const char* params) {
    (void)scheme;
    (void)name_len;
    (void)params;
    return nullptr;
}

static JsonObject make_button(JsonDocument& doc) {
    doc["cols"] = 1;
    doc["rows"] = 1;
    JsonObject button = doc["buttons"].to<JsonArray>().add<JsonObject>();
    button["col"] = 0;
    button["row"] = 0;
    return button;
}

int main() {
    std::printf("=== pad confirmation validation tests ===\n");

    {
        JsonDocument doc;
        make_button(doc);
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        button["confirm"] = true;
        button["confirm_text"] = "Turn off the studio?";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        button["confirm"] = "yes";
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "confirm must be boolean") == 0);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        button["confirm_text"] = 42;
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "confirm_text must be a string") == 0);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        button["widget_type"] = "test";
        button["confirm"] = true;
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "confirm is only supported on normal buttons") == 0);
    }

    {
        char valid[CONFIG_CONFIRM_TEXT_MAX_LEN];
        std::memset(valid, 'a', sizeof(valid) - 1);
        valid[sizeof(valid) - 1] = '\0';

        JsonDocument doc;
        JsonObject button = make_button(doc);
        button["confirm_text"] = valid;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    {
        char too_long[CONFIG_CONFIRM_TEXT_MAX_LEN + 1];
        std::memset(too_long, 'a', sizeof(too_long) - 1);
        too_long[sizeof(too_long) - 1] = '\0';

        JsonDocument doc;
        JsonObject button = make_button(doc);
        button["confirm_text"] = too_long;
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "confirm_text too long (max 127 chars)") == 0);
    }

    if (g_failures) {
        std::printf("%d test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All pad confirmation validation tests passed.\n");
    return 0;
}
#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

#include "binding_template.h"
#include "action_registry.h"
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
    static auto describe_test_widget = [](JsonObject& out) {
        JsonObject field = out["config_fields"].to<JsonArray>().add<JsonObject>();
        field["name"] = "widget_test_action";
        field["type"] = "action";
    };
    static WidgetType test_widget = {};
    if (type_name && std::strcmp(type_name, "test") == 0) {
        test_widget.name = "test";
        test_widget.describeSchema = describe_test_widget;
        return &test_widget;
    }
    return nullptr;
}

bool binding_template_scheme_known(const char* scheme, size_t name_len) {
    return !(name_len == 7 && std::strncmp(scheme, "unknown", name_len) == 0);
}

const char* binding_template_validate_params(const char* scheme, size_t name_len,
                                             const char* params) {
    (void)scheme;
    (void)name_len;
    (void)params;
    return nullptr;
}

const ActionTypeDef* action_type_find(const char* type) {
    static const ActionTypeDef registered_type = {
        "registered_test", nullptr, nullptr, nullptr, nullptr, nullptr
    };
    if (type && std::strcmp(type, registered_type.type_name) == 0) return &registered_type;
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
        make_button(doc);
        JsonArray actions = doc["pad_actions"].to<JsonArray>();
        actions.add<JsonObject>()["type"] = "none";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    {
        JsonDocument doc;
        make_button(doc);
        JsonArray actions = doc["pad_actions"].to<JsonArray>();
        actions.add<JsonObject>()["type"] = "registered_test";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    {
        JsonDocument doc;
        make_button(doc);
        doc["pad_actions"] = "screen";
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "pad_actions must be an array") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        JsonArray actions = doc["pad_actions"].to<JsonArray>();
        for (int i = 0; i < 4; i++) actions.add<JsonObject>()["type"] = "sound_alert";
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "too many actions (max 3)") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        doc["pad_actions"].to<JsonArray>().add<JsonObject>();
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "action missing type") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        doc["pad_actions"].to<JsonArray>().add("sound_alert");
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "action missing type") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        doc["pad_actions"].to<JsonArray>().add<JsonArray>();
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "action missing type") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        doc["pad_actions"].to<JsonArray>().add(nullptr);
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "action missing type") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        doc["pad_actions"].to<JsonArray>().add<JsonObject>()["type"] = "unknown";
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "unknown action type") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        JsonObject action = doc["pad_actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = "light.kitchen";
        action["service"] = "light.toggle";
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "service must be bare; use 'toggle'") == 0);
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

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "timer";
        action["timer_id"] = 1;
        action["timer_command"] = "start";
        action["timer_mode"] = "down";
        action["timer_value"] = "1234567890123456";
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "timer value too long") == 0);
    }

    const char* invalid_timer_bindings[] = {
        "[junk]", "[unknown:value]", "[mqtt:value"
    };
    for (const char* value : invalid_timer_bindings) {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "timer";
        action["timer_value"] = value;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "cycle_pad";
        action["direction"] = "previous";
        action["wrap"] = false;
        action["excluded_pads"] = "5, 1,5,bad,99";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    const char* cycle_field_errors[] = {
        "cycle_pad direction must be a string",
        "cycle_pad direction must be 'next' or 'previous'",
        "cycle_pad wrap must be boolean",
        "cycle_pad excluded_pads must be a string",
    };
    for (int field = 0; field < 4; ++field) {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "cycle_pad";
        if (field == 0) action["direction"] = 42;
        if (field == 1) action["direction"] = "sideways";
        if (field == 2) action["wrap"] = "true";
        if (field == 3) action["excluded_pads"].to<JsonArray>().add(1);
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          cycle_field_errors[field]) == 0);
    }

    const char* valid_services[] = {
        "media_stop",
        "volume_mute",
        "media_play_pause",
        "media_previous_track",
        "turn_on",
    };
    for (const char* service : valid_services) {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = "media_player.keuken";
        action["service"] = service;
        action["data_json"] = "{}";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = "media_player.keuken";
        action["service"] = "media_player.media_play_pause";
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "service must be bare; use 'media_play_pause'") == 0);
    }

    const char* invalid_services[] = { "MediaStop", "media-stop" };
    for (const char* service : invalid_services) {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = "media_player.keuken";
        action["service"] = service;
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "ha_service service must contain only lowercase letters, digits, and '_'") == 0);
    }

    const char* missing_field_errors[] = {
        "ha_service missing entity_id",
        "ha_service missing service",
    };
    for (int missing = 0; missing < 2; ++missing) {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        if (missing != 0) action["entity_id"] = "light.kitchen";
        if (missing != 1) action["service"] = "toggle";
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          missing_field_errors[missing]) == 0);
    }

    const char* empty_field_errors[] = {
        "ha_service entity_id must not be empty",
        "ha_service service must not be empty",
    };
    for (int empty = 0; empty < 2; ++empty) {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = empty == 0 ? "" : "light.kitchen";
        action["service"] = empty == 1 ? "" : "toggle";
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          empty_field_errors[empty]) == 0);
    }

    const char* type_errors[] = {
        "ha_service entity_id must be a string",
        "ha_service service must be a string",
        "ha_service data_json must be a string",
    };
    for (int field = 0; field < 3; ++field) {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        if (field == 0) action["entity_id"] = 42;
        else action["entity_id"] = "light.kitchen";
        if (field == 1) action["service"] = 42;
        else action["service"] = "toggle";
        if (field == 2) action["data_json"].to<JsonObject>()["brightness_pct"] = 80;
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()), type_errors[field]) == 0);
    }

    const char* malformed_entity_ids[] = {
        "light", ".kitchen", "light.", "light.kitchen.extra", "light. kitchen"
    };
    for (const char* entity_id : malformed_entity_ids) {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = entity_id;
        action["service"] = "toggle";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        char entity_id[sizeof(((HaServicePayload*)nullptr)->entity_id) + 1];
        std::memset(entity_id, 'a', sizeof(entity_id) - 1);
        entity_id[5] = '.';
        entity_id[sizeof(entity_id) - 1] = '\0';
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = entity_id;
        action["service"] = "toggle";
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "ha_service entity_id too long") == 0);
    }

    {
        char service[sizeof(((HaServicePayload*)nullptr)->service) + 1];
        std::memset(service, 'a', sizeof(service) - 1);
        service[sizeof(service) - 1] = '\0';
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = "light.kitchen";
        action["service"] = service;
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "ha_service service too long") == 0);
    }

    {
        char data_json[sizeof(((HaServicePayload*)nullptr)->data_json) + 1];
        std::memset(data_json, ' ', sizeof(data_json) - 1);
        data_json[0] = '{';
        data_json[1] = '}';
        data_json[sizeof(data_json) - 1] = '\0';
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = "light.kitchen";
        action["service"] = "toggle";
        action["data_json"] = data_json;
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "ha_service data_json too long") == 0);
    }

    const char* rejected_data_json[] = {
        "{", "[]", "42", "true", "null"
    };
    for (const char* data_json : rejected_data_json) {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = "light.kitchen";
        action["service"] = "toggle";
        action["data_json"] = data_json;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    const char* accepted_data_json[] = { "", "{}", "{\"brightness_pct\":80}" };
    for (const char* data_json : accepted_data_json) {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = "light.kitchen";
        action["service"] = "turn_on";
        action["data_json"] = data_json;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = "light.kitchen";
        action["service"] = "toggle";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["lp_actions"].to<JsonArray>().add<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = "light.kitchen";
        action["service"] = "light.toggle";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        button["widget_type"] = "test";
        JsonObject action = button["widget_test_action"].to<JsonObject>();
        action["type"] = "ha_service";
        action["entity_id"] = "light.kitchen";
        action["service"] = "light.toggle";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    if (g_failures) {
        std::printf("%d test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All pad confirmation validation tests passed.\n");
    return 0;
}
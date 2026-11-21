#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "binding_template.h"
#include "action_registry.h"
#include "pad_validate.h"
#include "pad_composition.h"
#include "widgets/widget_registry.h"
#include "pad_json.h"

static int g_failures = 0;
static bool g_contextual_list = false;
static bool g_context_called = false;
static bool g_context_received_template = false;

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
    static WidgetType sparkline_widget = {};
    static WidgetType rocker_widget = {};
    static WidgetType list_widget = {};
    if (type_name && std::strcmp(type_name, "list") == 0) {
        list_widget.name = "list";
        list_widget.validatePadContext = g_contextual_list
            ? +[](JsonObjectConst, const PadComposedContext& context) -> const char* {
                g_context_called = true;
                g_context_received_template = context.button_count == 1;
                return nullptr;
            } : nullptr;
        list_widget.collectionBinding = g_contextual_list;
        return &list_widget;
    }
    if (type_name && (std::strcmp(type_name, "rocker") == 0 || std::strcmp(type_name, "numericrocker") == 0)) {
        rocker_widget.name = type_name;
        return &rocker_widget;
    }
    if (type_name && std::strcmp(type_name, "sparkline") == 0) {
        sparkline_widget.name = "sparkline";
        return &sparkline_widget;
    }
    if (type_name && std::strcmp(type_name, "test") == 0) {
        test_widget.name = "test";
        test_widget.describeSchema = describe_test_widget;
        return &test_widget;
    }
    return nullptr;
}

uint8_t widget_count() { return 1; }
const WidgetType* widget_at(uint8_t index) { return index == 0 ? widget_find("list") : nullptr; }

bool binding_template_scheme_known(const char* scheme, size_t name_len) {
    return !(name_len == 7 && std::strncmp(scheme, "unknown", name_len) == 0);
}

bool binding_template_has_bindings(const char* text) {
    return text && std::strstr(text, "[mqtt:");
}

bool binding_template_resolve_checked(const char*, char*, size_t) { return false; }
bool binding_template_well_formed(const char*) { return true; }
const ActionOperationDef* action_operation_find(const char* id, const ActionTypeDef**) {
    static const ActionArgumentDef screen_arguments[] = {
        {"target", "Screen", ACTION_ARGUMENT_SCREEN, 0, 0, 31, true, "[item:id]", "screens"}
    };
    static const ActionArgumentDef timer_arguments[] = {
        {"timer", "Timer", ACTION_ARGUMENT_INTEGER, 1, 3, 0, true, "[var:timer]", "timers"},
        {"seconds", "Seconds", ACTION_ARGUMENT_INTEGER, INT32_MIN, INT32_MAX, 0, true, "[event:step]", "number"}
    };
    static const ActionOperationDef operations[] = {
        {"screen.open", "Navigate to screen", screen_arguments, 1, nullptr},
        {"timer.adjust", "Adjust timer", timer_arguments, 2, nullptr}
    };
    for (const auto& operation : operations)
        if (id && !std::strcmp(id, operation.id)) return &operation;
    return nullptr;
}

const char* binding_template_validate_params(const char* scheme, size_t name_len,
                                             const char* params) {
    (void)scheme;
    (void)name_len;
    (void)params;
    return nullptr;
}

static const ActionTypeDef registered_type = {
    "registered_test", nullptr
};

const ActionTypeDef* action_type_find(const char* type) {
    if (type && std::strcmp(type, registered_type.type_name) == 0) return &registered_type;
    return nullptr;
}

bool action_type_is_supported(const char* type) {
    return action_type_find(type) != nullptr;
}

static JsonObject make_button(JsonDocument& doc) {
    doc["cols"] = 1;
    doc["rows"] = 1;
    JsonObject button = doc["buttons"].to<JsonArray>().add<JsonObject>();
    button["col"] = 0;
    button["row"] = 0;
    return button;
}

class String {
public:
    String& operator=(const char* value) { text = value; return *this; }
    std::string text;
};
class AsyncWebServer;

#define DISPLAY_MANAGER_H
class DisplayManager {
public:
    uint16_t getActiveWidth() const { return DISPLAY_WIDTH; }
    uint16_t getActiveHeight() const { return DISPLAY_HEIGHT; }
};
static DisplayManager* displayManager = nullptr;

#include "mcp_tool_registry.h"
#undef REGISTER_MCP_TOOL
#define REGISTER_MCP_TOOL(var)
#include "../src/app/mcp_tools_pads.cpp"

static std::string stored_pad;
static std::string active_pad;
static JsonDocument template_pad;
static unsigned save_calls = 0, cache_rebuilds = 0, subscription_refreshes = 0;
static unsigned saved_pad_reads = 0, internal_allocations = 0, outstanding_allocations = 0;
static bool fail_allocations = false;
static LoopBridgeResult bridge_status = LOOP_BRIDGE_OK;

unsigned long millis() { return 0; }
bool psramFound() { return true; }
void* heap_caps_malloc(size_t size, uint32_t caps) {
    void* pointer = fail_allocations ? nullptr : std::malloc(size);
    if (pointer) ++outstanding_allocations;
    if (pointer && (caps & MALLOC_CAP_INTERNAL)) ++internal_allocations;
    return pointer;
}
void heap_caps_free(void* pointer) {
    if (pointer) --outstanding_allocations;
    std::free(pointer);
}
void* mcp_psram_alloc(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
bool mcp_tool_fail(JsonObject& result, String& error, int code, const char* message) {
    result[MCP_RESULT_ERRCODE_KEY] = code;
    error = message;
    return false;
}
bool mcp_finish_control(McpControlResult status, bool ok, const char* message,
                        JsonObject& result, String& error) {
    if (status == MCP_CONTROL_UNAVAILABLE)
        return mcp_tool_fail(result, error, MCP_RPC_ERR_INTERNAL, "control dispatch unavailable");
    if (status != MCP_CONTROL_OK || !ok)
        return mcp_tool_fail(result, error, MCP_RPC_ERR_INTERNAL, message);
    result["ok"] = true;
    return true;
}
LoopBridgeResult loop_bridge_dispatch(LoopBridgeExec exec, const void* context, size_t length,
                                      uint32_t, bool* ok, char* message, size_t capacity,
                                      LoopBridgeCleanup) {
    CHECK(length <= LOOP_BRIDGE_CTX_BYTES);
    if (bridge_status != LOOP_BRIDGE_OK) return bridge_status;
    exec(context, ok, message, capacity);
    return LOOP_BRIDGE_OK;
}
const ButtonDefaults* button_defaults_get() {
    static const ButtonDefaults defaults = {};
    return &defaults;
}
int pad_config_resolve_ref(const char* screen, char* error, size_t capacity) {
    if (screen && !std::strcmp(screen, "pad_0")) return 0;
    strlcpy(error, "unknown pad", capacity);
    return -1;
}
bool pad_config_exists(uint8_t) { return !stored_pad.empty(); }
char* pad_config_read_raw(uint8_t, size_t* length) {
    ++saved_pad_reads;
    *length = stored_pad.size();
    return stored_pad.empty() ? nullptr : strdup(stored_pad.c_str());
}
const char* pad_validate_with_template(JsonObjectConst pad, int, bool tolerate_offgrid,
                                       const PadValidationGeometry* geometry) {
    const char* error = pad_validate_action_version(pad);
    return error ? error : pad_validate(pad, tolerate_offgrid, geometry,
                                        template_pad.as<JsonObjectConst>());
}
bool pad_config_save_raw(uint8_t, const uint8_t* json, size_t length,
                         const char** error, const PadValidationGeometry* geometry) {
    ++save_calls;
    JsonDocument candidate;
    if (deserializeJson(candidate, json, length)) return false;
    *error = pad_validate_with_template(candidate.as<JsonObjectConst>(), 0, true, geometry);
    if (*error) return false;
    stored_pad.assign(reinterpret_cast<const char*>(json), length);
    active_pad = stored_pad;
    return true;
}
void pad_config_rebuild_all_caches() { ++cache_rebuilds; active_pad = stored_pad; }
void mqtt_sub_store_subscribe_all() { ++subscription_refreshes; }

static bool call_pad_tool(McpToolHandler handler, const char* input,
                          const char* expected_error = nullptr) {
    JsonDocument arguments, output;
    CHECK(!deserializeJson(arguments, input));
    JsonObject args = arguments.as<JsonObject>();
    JsonObject result = output.to<JsonObject>();
    String error;
    const bool ok = handler(args, result, error);
    CHECK(ok ? result["ok"] == true : !error.text.empty());
    if (expected_error) CHECK(!ok && error.text.find(expected_error) != std::string::npos);
    return ok;
}

static void test_pad_replacement() {
    stored_pad = R"({"action_format_version":1,"name":"Old","cols":4,"rows":4,"bg_color":"#123456","template_pad":2,"buttons":[{"col":3,"row":3}],"overlay_buttons":[{"col":2,"row":3}],"bindings":{"old":"[mqtt:old]"},"pad_actions":[{"op":"screen.open","args":{"target":"pad_1"}}]})";
    active_pad = stored_pad;
    CHECK(call_pad_tool(tool_replace_pad, R"({"screen":"pad_0","pad":{"action_format_version":1,"cols":2,"rows":2,"buttons":[{"col":1,"row":1,"label_center":"[pad:new]","actions":[{"op":"screen.open","args":{"target":"pad_1"}}]}],"overlay_buttons":[{"col":0,"row":0}],"bindings":{"new":"[mqtt:new]"},"pad_actions":[{"op":"timer.adjust","args":{"timer":1,"seconds":15}}]}})"));
    CHECK(save_calls == 1 && cache_rebuilds == 0 && subscription_refreshes == 1);
    CHECK(stored_pad == active_pad && saved_pad_reads == 0);
    JsonDocument saved;
    CHECK(!deserializeJson(saved, stored_pad));
    CHECK(saved["cols"] == 2 && saved["rows"] == 2);
    CHECK(saved["buttons"].size() == 1 && saved["overlay_buttons"].size() == 1);
    CHECK(saved["buttons"][0]["col"] == 1 && saved["overlay_buttons"][0]["row"] == 0);
    CHECK(saved["buttons"][0]["actions"].size() == 1 && saved["pad_actions"].size() == 1);
    CHECK(saved["bindings"].size() == 1 && saved["bindings"]["new"] == "[mqtt:new]");
    CHECK(!saved.containsKey("name") && !saved.containsKey("bg_color") &&
          !saved.containsKey("template_pad"));

    const std::string before = stored_pad;
    for (const char* invalid : {
        R"({"screen":"pad_0","pad":{"cols":2,"rows":2,"buttons":[{"col":2,"row":0}]}})",
        R"({"screen":"pad_0","pad":{"cols":2,"rows":2,"overlay_buttons":[{"col":0,"row":2}]}})",
        R"({"screen":"pad_0","pad":{"cols":2,"rows":2,"pad_actions":[{"op":"screen.open","args":{"target":"pad_1"}}]}})",
        R"({"screen":"pad_0","pad":{"buttons":[{"bg_image_password_set":true}]}})",
        R"({"screen":"pad_0","pad":{"overlay_buttons":[{"bg_image_password_set":"true"}]}})",
        R"({"screen":"pad_0","pad":[]})",
        R"({"screen":"not_a_pad","pad":{}})"
    }) {
        CHECK(!call_pad_tool(tool_replace_pad, invalid));
        CHECK(stored_pad == before && active_pad == before && save_calls == 1);
    }
    bridge_status = LOOP_BRIDGE_BUSY;
    CHECK(!call_pad_tool(tool_replace_pad, R"({"screen":"pad_0","pad":{"cols":1,"rows":1}})", "busy"));
    CHECK(outstanding_allocations == 0);
    bridge_status = LOOP_BRIDGE_UNAVAILABLE;
    CHECK(!call_pad_tool(tool_replace_pad, R"({"screen":"pad_0","pad":{"cols":1,"rows":1}})", "unavailable"));
    CHECK(outstanding_allocations == 0);
    bridge_status = LOOP_BRIDGE_OK;
    fail_allocations = true;
    CHECK(!call_pad_tool(tool_replace_pad, R"({"screen":"pad_0","pad":{"cols":1,"rows":1}})", "out of memory"));
    fail_allocations = false;
    CHECK(stored_pad == before && active_pad == before && save_calls == 1);
    CHECK(internal_allocations == 0);

    CHECK(call_pad_tool(tool_set_pad, R"({"screen":"pad_0","pad_name":"Partial"})"));
    CHECK(!deserializeJson(saved, stored_pad));
    CHECK(saved["name"] == "Partial" && saved["buttons"].size() == 1 &&
          saved["overlay_buttons"].size() == 1 && saved["bindings"].size() == 1);
    CHECK(call_pad_tool(tool_replace_pad, R"({"screen":"pad_0","pad":{"cols":1,"rows":1}})"));
    CHECK(stored_pad == R"({"cols":1,"rows":1})" && active_pad == stored_pad);

    CHECK(call_pad_tool(tool_replace_pad, R"({"screen":"pad_0","pad":{"cols":1,"rows":1,"buttons":[{"col":0,"row":0,"bg_image_url":"http://camera.test/image","bg_image_password":"explicit","bg_image_password_set":false}],"overlay_buttons":[{"col":0,"row":0,"bg_image_password":"overlay"}]}})"));
    CHECK(!deserializeJson(saved, stored_pad));
    CHECK(saved["buttons"][0]["bg_image_password"] == "explicit");
    CHECK(!saved["buttons"][0].containsKey("bg_image_password_set"));
    pad_redact_image_passwords(saved.as<JsonObject>());
    CHECK(!saved["buttons"][0].containsKey("bg_image_password") &&
          saved["buttons"][0]["bg_image_password_set"] == true);
    CHECK(!saved["overlay_buttons"][0].containsKey("bg_image_password") &&
          saved["overlay_buttons"][0]["bg_image_password_set"] == true);
    CHECK(call_pad_tool(tool_replace_pad, R"({"screen":"pad_0","pad":{"cols":1,"rows":1,"buttons":[{"col":0,"row":0,"bg_image_url":"http://camera.test/image","bg_image_password_set":false}]}})"));
    CHECK(!deserializeJson(saved, stored_pad));
    CHECK(!saved["buttons"][0].containsKey("bg_image_password"));
    pad_redact_image_passwords(saved.as<JsonObject>());
    CHECK(saved["buttons"][0]["bg_image_password_set"] == false);
}

int main() {
    std::printf("=== pad confirmation validation tests ===\n");

    {
        JsonDocument doc;
        doc["cols"] = 1;
        doc["rows"] = 1;
        doc["bg_image_path"] = "/images/background.jpg";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        for (const char* path : {"[image:current]", "https://example.test/background.jpg",
                                 "/images/../secret.png"}) {
            doc["bg_image_path"] = path;
            CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        }
        doc["bg_image_path"] = 42;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        doc["bg_image_path"] = "";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    {
        std::ifstream file("docs/samples/action-framework-gate1.json");
        CHECK(file.is_open());
        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        JsonDocument doc;
        CHECK(!deserializeJson(doc, source));
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>()) == nullptr);
        g_contextual_list = true;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        g_contextual_list = false;
    }

    {
        JsonDocument doc;
        CHECK(!deserializeJson(doc, R"({"action_format_version":1,"buttons":[{"col":0,"row":0,"actions":[{"op":"screen.open\u0000other","args":{"target":"pad_1"}}]}]})"));
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["op"] = "screen.open";
        action["args"]["target"] = "pad_1";
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>()) != nullptr);
        doc["action_format_version"] = 1;
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>()) == nullptr);
        action["args"]["target"]["$template"] = "[item:id]";
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>()) == nullptr);
        action["type"] = "screen";
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>()) != nullptr);
        action["type"] = nullptr;
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>()) != nullptr);
        action.remove("type");
        doc["action_format_version"] = 2;
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>()) != nullptr);
        doc["action_format_version"] = 1;
        action["args"]["extra"] = true;
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>()) != nullptr);
        action["args"].as<JsonObject>().remove("extra");
        button["actions"].to<JsonArray>().add(nullptr);
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        doc["action_format_version"] = 1;
        JsonObject button = make_button(doc);
        JsonArray actions = button["actions"].to<JsonArray>();
        for (int i = 0; i < MAX_BUTTON_ACTIONS; ++i) {
            JsonObject action = actions.add<JsonObject>();
            action["op"] = "screen.open";
            action["args"]["target"] = "pad_1";
        }
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>(), false) == nullptr);
        JsonObject overflow = actions.add<JsonObject>();
        overflow["op"] = "screen.open";
        overflow["args"]["target"] = "pad_1";
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>(), false) != nullptr);
        doc["pad_actions"] = actions;
        button.remove("actions");
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>(), false) != nullptr);
        doc.remove("pad_actions");
        doc["overlay_buttons"].to<JsonArray>().add(42);
        CHECK(pad_validate_action_version(doc.as<JsonObjectConst>(), false) != nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        button["widget_type"] = "list";
        button["widget_data_binding"] = "[unknown:collection]";
        g_contextual_list = false;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        g_contextual_list = true;
        g_context_called = false;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        CHECK(g_context_called);
        doc["template_pad"] = 1;
        g_context_called = false;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        CHECK(!g_context_called);
        JsonDocument inherited;
        inherited.to<JsonObject>();
        CHECK(pad_validate(doc.as<JsonObjectConst>(), false, nullptr,
                           inherited.as<JsonObjectConst>()) == nullptr);
        CHECK(g_context_called && g_context_received_template);
        g_contextual_list = false;
    }

    for (const char* layer : {"buttons", "overlay_buttons"}) {
        JsonDocument doc;
        make_button(doc);
        JsonObject button = doc[layer].to<JsonArray>().add<JsonObject>();
        button["col"] = 0;
        button["row"] = 0;
        button["bg_color"] = "transparent";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["bg_color"] = "#333333";
        for (unsigned opacity : {0u, 50u, 100u}) {
            button["bg_opacity"] = opacity;
            CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        }
        button.remove("bg_color");
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["bg_color"] = "#333333";
        for (int opacity : {-1, 101}) {
            button["bg_opacity"] = opacity;
            CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        }
        button["bg_opacity"] = 50.5;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["bg_opacity"] = "[mqtt:opacity]";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button.remove("bg_opacity");
        for (const char* field : {"fg_color", "border_color", "icon_color",
                                  "icon_badge_color", "bg_pattern_color"}) {
            button[field] = "transparent";
            CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
            button.remove(field);
        }
        button["bg_color"] = "[mqtt:background|#333333]";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["bg_color"] = "translucent";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        for (const char* fit : {"fill-center", "fill-top", "fill-bottom",
                                "fill-left", "fill-right", "letterbox"}) {
            button["bg_image_fit"] = fit;
            CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        }
        button["bg_image_fit"] = "stretch";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["bg_image_fit"] = 42;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        button["icon_badge_padding"] = 0;
        button["icon_badge_color"] = "[health:color]";
        button["widget_type"] = "sparkline";
        button["widget_sparkline_fill_opacity"] = 100;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["icon_badge_padding"] = 65;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["icon_badge_padding"] = 1.5;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["icon_badge_padding"] = 64;
        button["icon_badge_color"] = "red";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["icon_badge_color"] = std::string(CONFIG_COLOR_MAX_LEN, 'a');
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["icon_badge_color"] = "auto";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        for (const char* color : {"auto", "#123456", "[health:color]"}) {
            button["icon_color"] = color;
            CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        }
        button["icon_color"] = "red";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["icon_color"] = std::string(CONFIG_COLOR_MAX_LEN, 'a');
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button.remove("icon_color");
        for (const char* invalid : {"-1", "101", "1.5", "true", "\"50\""}) {
            JsonDocument value;
            CHECK(!deserializeJson(value, invalid));
            button["widget_sparkline_fill_opacity"] = value.as<JsonVariant>();
            CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        }
        button["widget_sparkline_fill_opacity"] = 0;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["widget_sparkline_height_pct"] = 10;
        button["widget_sparkline_fill_opacity_2"] = 0;
        button["widget_sparkline_line_width_1"] = 10;
        button["widget_sparkline_current_dot_3"] = 20;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["widget_sparkline_height_pct"] = 9;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["widget_sparkline_height_pct"] = 100;
        button["widget_sparkline_fill_opacity_2"] = 101;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["widget_sparkline_fill_opacity_2"] = 0;
        button["widget_sparkline_line_width_1"] = 0;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["widget_sparkline_line_width_1"] = 1;
        button["widget_sparkline_current_dot_3"] = 1.5;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        button["icon_position"] = "right";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["icon_position"] = "below";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["widget_position"] = "left";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["widget_type"] = "sparkline";
        button["group_size_pct"] = 40;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["widget_position"] = "below";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        for (const char* position : {"overlay-top-left", "overlay-top-center", "overlay-top-right",
                                     "overlay-center-left", "overlay-center", "overlay-center-right",
                                     "overlay-bottom-left", "overlay-bottom-center", "overlay-bottom-right"}) {
            button["widget_position"] = position;
            CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        }
        button["widget_position"] = "overlay-invalid";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["widget_position"] = "below";
        button["group_size_pct"] = 91;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["group_size_pct"] = 0;
        button["reserve_label_slots"] = true;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["reserve_label_slots"] = "true";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button.remove("reserve_label_slots");
        button["overlay_backdrop"] = true;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["overlay_backdrop"] = false;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["overlay_backdrop"] = "true";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button.remove("overlay_backdrop");
        button["icon_position"] = "center";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["icon_position"] = "overlay-top-left";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["icon_position"] = "left";
        button["widget_type"] = "test";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["rocker_group_layout"] = "beside";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["widget_type"] = "rocker";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["widget_type"] = "numericrocker";
        button["rocker_group_layout"] = "center";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["rocker_group_layout"] = "invalid";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        button["bg_image_url"] = "https://images.example/[mqtt:cover].jpg";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["bg_image_user"] = "user";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button.remove("bg_image_user");
        button["bg_image_password_set"] = true;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button.remove("bg_image_password_set");
        button["bg_image_url"] = std::string(256, 'x');
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["bg_image_url"] = "https://images.example/cover.jpg";
        button["bg_image_user"] = "user";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    {
        JsonDocument doc;
        doc["cols"] = 3;
        doc["rows"] = 2;
        doc["column_widths_px"].to<JsonArray>().add(0);
        doc["column_widths_px"].add(20);
        doc["column_widths_px"].add(0);
        doc["row_heights_px"].to<JsonArray>().add(25);
        doc["row_heights_px"].add(0);
        const PadValidationGeometry geometry = {101, 100, 2, 0, 0, 0, 0, 0};
        CHECK(pad_validate(doc.as<JsonObjectConst>(), false, &geometry) == nullptr);

        doc["column_widths_px"][0] = 30;
        doc["column_widths_px"][1] = 30;
        doc["column_widths_px"][2] = 30;
        CHECK(pad_validate(doc.as<JsonObjectConst>(), false, &geometry) != nullptr);

        doc["column_widths_px"][0] = 96;
        doc["column_widths_px"][1] = 0;
        doc["column_widths_px"][2] = 0;
        CHECK(pad_validate(doc.as<JsonObjectConst>(), false, &geometry) != nullptr);

        doc["column_widths_px"][0] = 0.5;
        CHECK(pad_validate(doc.as<JsonObjectConst>(), false, &geometry) != nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["op"] = "screen.open";
        action["args"]["target"]["$template"] = "[unknown:value]";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        action["args"]["target"]["$template"] = "[event:step]";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        action.clear();
        action["type"] = "screen";
        action["target"] = "pad_1";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        make_button(doc);
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        doc["cols"] = 10;
        doc["rows"] = 10;
        button["col"] = 8;
        button["row"] = 8;
        button["col_span"] = 2;
        button["row_span"] = 2;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        button["col"] = 9;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button["col_span"] = 1;
        button["row"] = 9;
        button["row_span"] = 1;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        doc["cols"] = 11;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        doc["cols"] = 10;
        doc["rows"] = 11;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject icon = button["icon"].to<JsonObject>();
        icon["source_id"] = "s_0123456789abcdef01234567";
        icon["catalog_id"] = "material-symbols:lightbulb";
        icon["kind"] = "mono";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);

        icon["source_id"] = "s_0123456789abcdef0123456g";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        icon["source_id"] = "s_0123456789abcdef01234567";
        icon["kind"] = "color";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        button.remove("icon");
        button["icon_id"] = "mi_lightbulb";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        doc["cols"] = 2;
        doc["rows"] = 1;
        JsonObject main_button = doc["buttons"].to<JsonArray>().add<JsonObject>();
        main_button["col"] = 0;
        main_button["row"] = 0;
        JsonObject overlay_button = doc["overlay_buttons"].to<JsonArray>().add<JsonObject>();
        overlay_button["col"] = 0;
        overlay_button["row"] = 0;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);

        JsonObject overlapping_overlay = doc["overlay_buttons"].as<JsonArray>().add<JsonObject>();
        overlapping_overlay["col"] = 0;
        overlapping_overlay["row"] = 0;
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "buttons overlap within a layer") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        doc["overlay_buttons"] = "invalid";
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "overlay_buttons must be an array") == 0);
    }

    {
        JsonDocument doc;
        doc["cols"] = MAX_GRID_COLS;
        doc["rows"] = MAX_GRID_ROWS;
        JsonArray buttons = doc["buttons"].to<JsonArray>();
        JsonArray overlays = doc["overlay_buttons"].to<JsonArray>();
        for (int index = 0; index < MAX_PAD_BUTTONS; ++index) {
            JsonObject button = buttons.add<JsonObject>();
            button["col"] = index;
            button["row"] = 0;
        }
        JsonObject extra = overlays.add<JsonObject>();
        extra["col"] = 0;
        extra["row"] = 0;
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "too many buttons across layers") == 0);
    }

    {
        JsonDocument doc;
        doc["cols"] = MAX_GRID_COLS;
        doc["rows"] = MAX_GRID_ROWS;
        JsonArray buttons = doc["buttons"].to<JsonArray>();
        JsonArray overlays = doc["overlay_buttons"].to<JsonArray>();
        for (int index = 0; index < MAX_PAD_BUTTONS; ++index) {
            JsonObject button = index == 0
                ? buttons.add<JsonObject>()
                : overlays.add<JsonObject>();
            button["col"] = index % MAX_GRID_COLS;
            button["row"] = index / MAX_GRID_COLS;
        }
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
    }

    {
        JsonDocument doc;
        make_button(doc);
        JsonArray actions = doc["pad_actions"].to<JsonArray>();
        actions.add<JsonObject>()["type"] = "none";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        make_button(doc);
        JsonArray actions = doc["pad_actions"].to<JsonArray>();
        JsonObject action = actions.add<JsonObject>();
        action["op"] = "screen.open";
        action["args"]["target"] = "pad_1";
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
                          "operation is required") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        doc["pad_actions"].to<JsonArray>().add("sound_alert");
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "operation is required") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        doc["pad_actions"].to<JsonArray>().add<JsonArray>();
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "operation is required") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        doc["pad_actions"].to<JsonArray>().add(nullptr);
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "operation is required") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        JsonObject action = doc["pad_actions"].to<JsonArray>().add<JsonObject>();
        action["op"] = "unknown";
        action["args"].to<JsonObject>();
        CHECK(std::strcmp(pad_validate(doc.as<JsonObjectConst>()),
                          "unknown or unavailable operation") == 0);
    }

    {
        JsonDocument doc;
        make_button(doc);
        JsonObject action = doc["pad_actions"].to<JsonArray>().add<JsonObject>();
        action["op"] = "screen.open";
        action["args"]["target"] = "pad_1";
        action["type"] = "screen";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button["actions"].to<JsonArray>().add<JsonObject>();
        action["op"] = "timer.adjust";
        action["args"]["timer"] = 4;
        action["args"]["seconds"] = 1;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        deserializeJson(doc, R"({"cols":3,"rows":2,"column_widths_px":[0,40,0],"buttons":[{"col":1,"row":0,"col_span":2,"row_span":1,"icon_scale_pct":75,"label_top":"Name","border_width":"2","content_pad":"4","widget_type":"gauge"}]})");
        JsonObject button = doc["buttons"][0];
        const uint32_t original = pad_icon_render_key(doc.as<JsonObjectConst>(), button);
        CHECK(original == 0xac65574eu);
        button["label_top"] = "Other text";
        button["fg_color"] = "ff0000";
        CHECK(pad_icon_render_key(doc.as<JsonObjectConst>(), button) == original);
        button["icon_scale_pct"] = 80;
        CHECK(pad_icon_render_key(doc.as<JsonObjectConst>(), button) != original);
        button["icon_scale_pct"] = 75;
        doc["column_widths_px"][1] = 50;
        CHECK(pad_icon_render_key(doc.as<JsonObjectConst>(), button) != original);
    }

    {
        JsonDocument doc;
        deserializeJson(doc, R"({"cols":10,"rows":10,"column_widths_px":[0,0,0,0,0,0,0,0,21,0],"row_heights_px":[0,0,0,0,0,0,0,0,0,31],"buttons":[{"col":9,"row":9}]})");
        JsonObject button = doc["buttons"][0];
        const uint32_t original = pad_icon_render_key(doc.as<JsonObjectConst>(), button);
        CHECK(original == 0x027ad4aau);
        doc["column_widths_px"][8] = 22;
        CHECK(pad_icon_render_key(doc.as<JsonObjectConst>(), button) != original);
    }

    {
        JsonDocument doc;
        deserializeJson(doc, R"({"cols":3,"rows":2,"column_widths_px":[0,40,0],"buttons":[{"col":1,"row":0,"col_span":2,"widget_type":"option_selector","widget_option_label":"Cool","widget_option_icon_5":{"kind":"mono"},"widget_option_icon_size_pct":75,"widget_option_icon_badge_padding":4}]})");
        CHECK(pad_option_icon_render_key(doc.as<JsonObjectConst>(), doc["buttons"][0], 5) == 0xe3f58eb6u);
        doc["buttons"][0]["reserve_label_slots"] = true;
        CHECK(pad_option_icon_render_key(doc.as<JsonObjectConst>(), doc["buttons"][0], 5) == 0xf3611d57u);
        doc["buttons"][0].remove("reserve_label_slots");
        doc["buttons"][0]["widget_option_inset"] = 0;
        CHECK(pad_option_icon_render_key(doc.as<JsonObjectConst>(), doc["buttons"][0], 5) == 0x3cf96935u);
        doc["buttons"][0]["widget_option_height_pct"] = 50;
        CHECK(pad_option_icon_render_key(doc.as<JsonObjectConst>(), doc["buttons"][0], 5) == 0xf09f7bd1u);
        doc["buttons"][0]["widget_option_weight"] = 2;
        doc["buttons"][0]["widget_option_weight_3"] = 7;
        CHECK(pad_option_icon_render_key(doc.as<JsonObjectConst>(), doc["buttons"][0], 5) == 0x2a6ad685u);
    }

    for (const char* field : {"actions", "lp_actions"}) {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        JsonObject action = button[field].to<JsonArray>().add<JsonObject>();
        action["op"] = "timer.adjust";
        action["args"]["timer"] = 1;
        action["args"]["seconds"] = 15;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        action["args"]["seconds"].to<JsonObject>()["$template"] = "[unknown:value]";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
        action["args"]["seconds"]["$template"] = "[event:step]";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        action["args"]["timer"] = 4;
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        JsonObject button = make_button(doc);
        button["widget_type"] = "test";
        JsonObject action = button["widget_test_action"].to<JsonObject>();
        action["op"] = "screen.open";
        action["args"]["target"]["$template"] = "[unknown:value]";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) != nullptr);
    }

    {
        JsonDocument doc;
        make_button(doc);
        JsonObject bindings = doc["bindings"].to<JsonObject>();
        bindings["power"] = "[mqtt:home/solar/power;watts;%.0f]";
        bindings["hot"] = "[expr:[mqtt:home/solar/power;watts]>3000?1:0]";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        bindings["hot"] = "[expr:[pad:power]>3000?1:0]";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        bindings["power"] = "[pad:hot]";
        CHECK(pad_validate(doc.as<JsonObjectConst>()) == nullptr);
        JsonDocument inherited;
        inherited["bindings"]["power"] = "[mqtt:home/solar/power;watts]";
        inherited["bindings"]["hot"] = "[expr:[pad:power]>3000?1:0]";
        PadComposedContext context;
        CHECK(pad_compose_context(doc.as<JsonObjectConst>(), inherited.as<JsonObjectConst>(), context) == nullptr);
        CHECK(context.binding_count == 2);
        CHECK(!std::strcmp(context.bindings[0].name, "power"));
        CHECK(!std::strcmp(context.bindings[0].value, "[pad:hot]"));
    }

    test_pad_replacement();

    if (g_failures) {
        std::printf("%d test(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All pad confirmation validation tests passed.\n");
    return 0;
}
#include <ArduinoJson.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        std::abort(); \
    } \
} while (false)

class String {
public:
    String& operator=(const char* value) { text = value; return *this; }
    std::string text;
};
class AsyncWebServer;
class File {
public:
    explicit operator bool() const { return false; }
    size_t size() const { return 0; }
    int read() { return -1; }
    size_t readBytes(char*, size_t) { return 0; }
    void close() {}
};
struct HostStorage {
    bool exists(const char*) const { return false; }
    File open(const char*, const char*) const { return {}; }
};
extern HostStorage Storage;
struct DeviceConfig;
DeviceConfig* web_portal_get_current_config();
std::unique_ptr<DynamicJsonDocument> make_psram_json_doc(size_t);

#define WEB_PORTAL_JSON_H
#define WEB_PORTAL_STATE_H
#define DISPLAY_MANAGER_H
#include "mcp_tool_registry.h"
#undef REGISTER_MCP_TOOL
#define REGISTER_MCP_TOOL(var)

// Keep the full tool implementation intact while dropping unrelated registrations.
#include "../src/app/mcp_tools_config.cpp"
#include "runtime_variables.h"
#include "var_binding.h"
#include "visual_alert.h"

static int bridge_calls;
static const char* bridge_variable;
static const char* bridge_value;
static int volume_calls;
static uint8_t volume;
static int alert_calls;
static int alert_stops;
static int wake_calls;
static VisualAlertParams alert;
static int notify_calls;
static int dismiss_calls;
static bool notification_available = true;
static std::string notification;
static uint32_t notification_duration;

bool mcp_tool_fail(JsonObject& result, String& err, int code, const char* message) {
    result[MCP_RESULT_ERRCODE_KEY] = code;
    err = message;
    return false;
}
bool mcp_run_control(McpControlExec exec, const void* ctx, size_t, uint32_t,
                     JsonObject& result, String& err) {
    ++bridge_calls;
    if (bridge_variable) {
        CHECK(runtime_variable_set(bridge_variable, bridge_value) == RUNTIME_VARIABLE_STORED);
        bridge_variable = nullptr;
    }
    bool ok = false;
    char message[MCP_TOOL_MSG_LEN] = {};
    exec(ctx, &ok, message, sizeof(message));
    if (!ok) return mcp_tool_fail(result, err, MCP_RPC_ERR_INTERNAL, message);
    result["ok"] = true;
    result["message"] = message;
    return true;
}
void display_manager_lock_if_needed(bool* locked) { *locked = false; }
void display_manager_unlock_if_needed(bool) {}
void display_manager_request_binding_refresh() {}
uint8_t audio_get_volume() { return volume; }
void audio_set_volume(uint8_t value) { ++volume_calls; volume = value; }
void screen_saver_manager_notify_activity(bool) { ++wake_calls; }
uint8_t visual_alert_pattern_from_str(const char* pattern) {
    return !strcmp(pattern, "blink") ? VA_PATTERN_BLINK :
           !strcmp(pattern, "solid") ? VA_PATTERN_SOLID : VA_PATTERN_BREATHE;
}
void visual_alert_show(const VisualAlertParams* params) { ++alert_calls; alert = *params; }
void visual_alert_stop() { ++alert_stops; }
bool device_dialog_notify(const char* text, uint32_t duration, uint32_t*) {
    ++notify_calls;
    notification = text;
    notification_duration = duration;
    return notification_available;
}
void device_dialog_dismiss_notification(uint32_t) { ++dismiss_calls; }

static bool call(McpToolHandler handler, const char* json) {
    JsonDocument input, output;
    CHECK(!deserializeJson(input, json));
    JsonObject args = input.as<JsonObject>();
    JsonObject result = output.to<JsonObject>();
    String error;
    return handler(args, result, error);
}

int main() {
    var_binding_init();
    CHECK(!call(tool_set_volume, R"({"value":"garbage"})"));
    CHECK(bridge_calls == 0 && volume_calls == 0);
    CHECK(runtime_variable_set("value", "bad") == RUNTIME_VARIABLE_STORED);
    CHECK(!call(tool_set_volume, R"({"value":"[var:value]"})"));
    CHECK(bridge_calls == 1 && volume_calls == 0);
    CHECK(call(tool_set_volume, R"({"value":42.6})"));
    CHECK(volume_calls == 1 && volume == 43);
    CHECK(runtime_variable_set("value", "1") == RUNTIME_VARIABLE_STORED);
    bridge_variable = "value";
    bridge_value = "-12.5";
    CHECK(call(tool_set_volume, R"({"mode":"adjust","value":"[var:value]"})"));
    CHECK(volume_calls == 2 && volume == 30);
    CHECK(call(tool_set_volume, R"({"value":"150"})"));
    CHECK(volume_calls == 3 && volume == 100);

    int before = bridge_calls;
    CHECK(!call(tool_visual_alert, R"({"color":"not-hex"})"));
    CHECK(bridge_calls == before && alert_calls == 0 && wake_calls == 0);
    CHECK(runtime_variable_set("color", "nope") == RUNTIME_VARIABLE_STORED);
    CHECK(!call(tool_visual_alert, R"({"color":"[var:color]"})"));
    CHECK(alert_calls == 0 && wake_calls == 0);
    CHECK(call(tool_visual_alert, R"({})"));
    CHECK(alert_calls == 1 && wake_calls == 1 && alert.color == 0xFF0000 &&
           alert.period_ms == VA_DEFAULT_PERIOD_MS && alert.intensity == VA_DEFAULT_INTENSITY);
    CHECK(runtime_variable_set("color", "#123456") == RUNTIME_VARIABLE_STORED);
    CHECK(call(tool_visual_alert, R"({"color":"[var:color]","pattern":"blink","period_ms":900,"intensity":40,"duration_ms":1000})"));
    CHECK(alert_calls == 2 && wake_calls == 2 && alert.color == 0x123456 &&
           alert.pattern == VA_PATTERN_BLINK && alert.period_ms == 900 &&
           alert.intensity == 40 && alert.duration_ms == 1000);
    CHECK(call(tool_visual_alert, R"({"color":"#00FF00"})"));
    CHECK(alert_calls == 3 && wake_calls == 3 && alert.color == 0x00FF00);
    CHECK(call(tool_visual_alert, R"({"op":"stop"})"));
    CHECK(alert_stops == 1 && wake_calls == 3);

    CHECK(runtime_variable_set("text", "Hello") == RUNTIME_VARIABLE_STORED);
    CHECK(call(tool_notify, R"({"text":"[var:text]","duration_ms":5000})"));
    CHECK(notify_calls == 1 && notification == "Hello" && notification_duration == 5000);
    CHECK(!call(tool_notify, R"({"text":"[var:missing]"})"));
    CHECK(notify_calls == 1);
    CHECK(call(tool_notify, R"({"text":""})"));
    CHECK(dismiss_calls == 1);
    notification_available = false;
    CHECK(!call(tool_notify, R"({"text":"failure"})"));
    CHECK(notify_calls == 2 && dismiss_calls == 1);
    return 0;
}

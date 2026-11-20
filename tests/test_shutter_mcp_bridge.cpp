#include "shutter_session_overrides/Arduino.h"
#include <ArduinoJson.h>
#include <ArduinoJson/Deserialization/Readers/ArduinoStringReader.hpp>
#include <memory>

class AsyncWebServer;

#include "psram_json_allocator.h"
#define WEB_PORTAL_JSON_H
static std::shared_ptr<BasicJsonDocument<PsramJsonAllocator>> make_psram_json_doc(size_t size) {
    return std::make_shared<BasicJsonDocument<PsramJsonAllocator>>(size);
}

#include "mcp_tool_registry.h"
#undef REGISTER_MCP_TOOL
#define REGISTER_MCP_TOOL(var)
#undef REGISTER_MCP_CLASS_SCENARIO
#define REGISTER_MCP_CLASS_SCENARIO(text)

#include "../src/app/mcp_tool_util.cpp"
#if IS_COFFEE_SCALE || IS_DARKROOM_TIMER
// Only needed to compile unrelated file tools, discarded by the host linker.
extern HostLittleFS Storage;
#endif
#if IS_COFFEE_SCALE
#include "../src/app/device_classes/coffee_scale/mcp/mcp_tools_coffee_scale.cpp"
#elif IS_DARKROOM_TIMER
#include "../src/app/device_classes/darkroom_timer/mcp/mcp_tools_darkroom.cpp"
#else
#include "../src/app/device_classes/shutter_tester/mcp/mcp_tools_shutter.cpp"

bool shutter_measure_is_valid_speed(const char*) { return true; }
uint8_t shutter_test_scripts_parse(ShutterTestParseResult*) { return 0; }
const ShutterTestScript* shutter_test_scripts_find(const ShutterTestParseResult*, const char* id) {
    static const ShutterTestScript script{};
    return id && !strcmp(id, "test") ? &script : nullptr;
}
#endif

#include <freertos/semphr.h>
#include <freertos/task.h>

void test_specialized_mcp_drain() {
    TaskHandle_t previous = test_freertos_current_task;
    test_freertos_current_task = reinterpret_cast<TaskHandle_t>(1);
    loop_bridge_drain();
    test_freertos_current_task = previous;
}

static void drain_control(SemaphoreHandle_t, TickType_t ticks) {
    if (ticks && ticks != portMAX_DELAY) test_specialized_mcp_drain();
}

static bool call_control(const McpTool& tool, const char* command, const char* value,
                         bool drain, int* error_code, String& error) {
    JsonDocument input, output;
    if (command) input["command"] = String(command);
    if (value) input["value"] = String(value);
    JsonObject args = input.as<JsonObject>();
    JsonObject result = output.to<JsonObject>();
    TaskHandle_t previous = test_freertos_current_task;
    test_freertos_current_task = reinterpret_cast<TaskHandle_t>(1);
    loop_bridge_init();
    test_freertos_current_task = reinterpret_cast<TaskHandle_t>(2);
    test_freertos_semaphore_take_hook = drain ? drain_control : nullptr;
    test_freertos_force_wait_timeout = !drain;
    bool ok = tool.handler(args, result, error);
    test_freertos_semaphore_take_hook = nullptr;
    test_freertos_force_wait_timeout = false;
    test_freertos_current_task = previous;
    if (error_code) *error_code = result[MCP_RESULT_ERRCODE_KEY] | 0;
    if (ok && (!result["ok"].as<bool>() || !(result["message"] | "")[0])) {
        if (error_code) *error_code = 0;
        return false;
    }
    return ok;
}

#if TEST_SHUTTER_OPERATIONS
bool test_shutter_mcp_control(const char* command, const char* value, bool drain, int* error_code) {
    String error;
    return call_control(s_tool_shutter_control, command, value, drain, error_code, error);
}

void test_shutter_mcp_drain() {
    test_specialized_mcp_drain();
}
#else
bool test_specialized_mcp_control(const char* name, const char* command, const char* value,
                                  int expected_code, bool drain, const char* expected_error) {
    const McpTool* const tools[] = {
#if IS_COFFEE_SCALE
        &s_tool_scale_control, &s_tool_brew_control,
#elif IS_DARKROOM_TIMER
        &s_tool_expose_control, &s_tool_strip_control, &s_tool_meter_control, &s_tool_print_control,
#endif
    };
    for (const McpTool* tool : tools) {
        if (strcmp(tool->name, name)) continue;
        int code = 0;
        String error;
        bool ok = call_control(*tool, command, value, drain, &code, error);
        if (ok == (expected_code == 0) && code == expected_code &&
            (ok ? error.isEmpty() : !error.isEmpty()) &&
            (!expected_error || strstr(error.c_str(), expected_error))) return true;
        std::printf("FAIL: %s(%s, %s): ok=%d, code=%d (expected %d), error=%s\n",
                    name, command ? command : "<missing>", value ? value : "<missing>",
                    ok, code, expected_code, error.c_str());
        return false;
    }
    std::printf("FAIL: unknown test tool %s\n", name);
    return false;
}
#endif

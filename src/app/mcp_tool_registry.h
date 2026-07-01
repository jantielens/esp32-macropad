#ifndef MCP_TOOL_REGISTRY_H
#define MCP_TOOL_REGISTRY_H

#include <ArduinoJson.h>

// ============================================================================
// MCP Tool Registry
// ============================================================================
//
// Extension seam for the built-in Model Context Protocol server. Any
// translation unit can self-register a tool via REGISTER_MCP_TOOL(); the
// static initializer runs at startup and the tool becomes visible through the
// /mcp endpoint's tools/list and callable through tools/call.
//
// Core (board-agnostic) tools live in mcp_tools_core.cpp at the sketch root, so
// arduino-cli compiles them directly. Device-class tools live under
// device_classes/<name>/ and are pulled in by the aggregation TU
// mcp_components.cpp (mirroring route_components.cpp).
//
// Threading contract:
//   - Tool handlers run on the AsyncWebServer (web) task.
//   - READ tools may read shared state using the same locking the portal/touch
//     paths already use; they must NOT block on network I/O.
//   - CONTROL tools (read_only == false) must NOT call action_dispatch, LVGL,
//     the display manager, or blocking I/O directly. They defer the work to the
//     main loop() via mcp_control_dispatch() (see web_mcp.h) and serialize the
//     result once it completes.
// ============================================================================

// Tool handler. Receives the parsed `arguments` object from the tools/call
// request, writes its result into `result` (serialized as the tool's JSON
// output), and returns true on success. On failure it returns false and sets
// `err` to a human-readable message.
//
// A handler may request a specific JSON-RPC error code by writing an integer to
// result[MCP_RESULT_ERRCODE_KEY] before returning false; the dispatcher reads
// and strips that key. When absent, handler failures map to -32603 (internal
// error) per the MCP/JSON-RPC contract.
typedef bool (*McpToolHandler)(const JsonObject& args, JsonObject& result, String& err);

// Key a handler may set on `result` to override the JSON-RPC error code used
// when it returns false. The dispatcher removes this key from the result.
#define MCP_RESULT_ERRCODE_KEY "__mcp_code"

// ----------------------------------------------------------------------------
// Canonical JSON-RPC 2.0 + MCP error codes. Single source of truth shared by
// the dispatcher (web_mcp.cpp) and every tool translation unit, so the numeric
// values are not duplicated as magic literals across files.
// ----------------------------------------------------------------------------
static constexpr int MCP_RPC_ERR_PARSE        = -32700;
static constexpr int MCP_RPC_ERR_INVALID_REQ  = -32600;
static constexpr int MCP_RPC_ERR_METHOD       = -32601;
static constexpr int MCP_RPC_ERR_PARAMS       = -32602;
static constexpr int MCP_RPC_ERR_INTERNAL     = -32603;
static constexpr int MCP_RPC_ERR_CONTROL_BUSY = -32001;  // server-defined (-32000..-32099)

struct McpTool {
    const char* name;
    const char* description;
    const char* input_schema_json;   // JSON Schema (string literal)
    McpToolHandler handler;
    bool read_only;                  // surfaced as readOnlyHint
    bool destructive;                // surfaced as destructiveHint
    bool requires_control;           // hidden/refused when mcp_control_enabled == false
    bool requires_authoring;         // hidden/refused when mcp_authoring_enabled == false
};

// Register a tool. The McpTool must have static storage duration (the registry
// stores the pointer, not a copy). Returns false if the registry is full
// (silently dropped) or `tool` is null.
bool mcp_tool_register(const McpTool* tool);

// Number of registered tools.
uint8_t mcp_tool_count();

// Number of tools that could not be registered because the registry was full.
uint16_t mcp_tool_dropped();

// Get a registered tool by index (0 .. mcp_tool_count()-1), or nullptr.
const McpTool* mcp_tool_at(uint8_t index);

// Find a registered tool by name, or nullptr.
const McpTool* mcp_tool_find(const char* name);

// ----------------------------------------------------------------------------
// Device-class scenario — one sentence describing what THIS device is FOR,
// appended to the MCP `initialize` instructions so the model understands the
// device's core use case (e.g. "a darkroom enlarger timer for B&W printing")
// instead of inferring the whole domain from individual tool names. Only the
// compiled device class sets it (IS_* variants are mutually exclusive), so at
// most one sentence ships and the generic macropad adds nothing. Keep it to a
// single concise sentence to avoid bloating the instructions.
void mcp_set_class_scenario(const char* text);
const char* mcp_class_scenario();  // nullptr when unset

#define REGISTER_MCP_TOOL(var) \
    static struct _McpReg_##var { _McpReg_##var() { mcp_tool_register(&var); } } _mcp_reg_##var;

// Register the compiled device class's core-use-case sentence (see above).
#define REGISTER_MCP_CLASS_SCENARIO(text) \
    static struct _McpScenarioReg { _McpScenarioReg() { mcp_set_class_scenario(text); } } _mcp_scenario_reg;

#endif // MCP_TOOL_REGISTRY_H

#include "mcp_tool_util.h"

#if HAS_MCP

#include <esp_heap_caps.h>

bool mcp_tool_fail(JsonObject& result, String& err, int code, const char* msg) {
    err = msg ? msg : "error";
    result[MCP_RESULT_ERRCODE_KEY] = code;
    return false;
}

bool mcp_finish_control(McpControlResult r, bool ok, const char* msg,
                        JsonObject& result, String& err) {
    if (r == MCP_CONTROL_BUSY) {
        return mcp_tool_fail(result, err, MCP_RPC_ERR_CONTROL_BUSY, "control busy, retry");
    }
    if (r == MCP_CONTROL_TIMEOUT) {
        return mcp_tool_fail(result, err, MCP_RPC_ERR_INTERNAL, "control dispatch timed out");
    }
    if (r == MCP_CONTROL_INVALID) {
        return mcp_tool_fail(result, err, MCP_RPC_ERR_INTERNAL, "invalid control dispatch");
    }
    if (r == MCP_CONTROL_UNAVAILABLE) {
        return mcp_tool_fail(result, err, MCP_RPC_ERR_INTERNAL, "control dispatch unavailable");
    }
    if (r == MCP_CONTROL_TOO_LARGE) {
        return mcp_tool_fail(result, err, MCP_RPC_ERR_INTERNAL, "control request too large");
    }
    if (!ok) {
        return mcp_tool_fail(result, err, MCP_RPC_ERR_INTERNAL, (msg && msg[0]) ? msg : "control failed");
    }
    result["ok"] = true;
    // Copy the message: `msg` is typically the caller's stack buffer, out of
    // scope by the time the dispatcher serializes the result. Assigning a String
    // forces ArduinoJson to duplicate the bytes (a const char* would dangle).
    if (msg && msg[0]) result["message"] = String(msg);
    return true;
}

bool mcp_run_control(McpControlExec exec, const void* ctx, size_t ctx_len,
                     uint32_t timeout_ms, JsonObject& result, String& err) {
    bool ok = false;
    char msg[MCP_TOOL_MSG_LEN];
    msg[0] = '\0';
    McpControlResult r = mcp_control_dispatch(exec, ctx, ctx_len, timeout_ms,
                                              &ok, msg, sizeof(msg));
    return mcp_finish_control(r, ok, msg, result, err);
}

void* mcp_psram_alloc(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}

#endif // HAS_MCP

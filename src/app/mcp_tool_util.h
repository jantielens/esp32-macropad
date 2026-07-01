#ifndef MCP_TOOL_UTIL_H
#define MCP_TOOL_UTIL_H

#include "board_config.h"

#if HAS_MCP

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "mcp_tool_registry.h"   // MCP_RESULT_ERRCODE_KEY, MCP_RPC_ERR_*
#include "web_mcp.h"             // McpControlExec, McpControlResult, mcp_control_dispatch

// ============================================================================
// MCP tool helpers — one implementation shared by every tool translation unit.
//
// The core, config, pad and device-class tool TUs previously each carried their
// own byte-identical copies of the fail helper, the control-dispatch "finish"
// mapper, and the PSRAM-preferred allocation pattern. These live here once so
// the behavior (and the deferred-control success envelope) is defined in a
// single place. Every function is HAS_MCP-gated to match the tool TUs.
// ============================================================================

// Message-buffer length for deferred-control result text. Matches the bridge's
// internal s_ctrl_msg buffer (web_mcp.cpp) so mcp_run_control never truncates on
// copy-back.
static constexpr size_t MCP_TOOL_MSG_LEN = 160;

// Set a JSON-RPC error code + human message on `result`/`err` and return false.
// Every tool handler's failure path funnels through this (previously duplicated
// per TU as tool_fail / cfg_fail / pad_fail / cs_fail / dr_fail / sh_fail).
bool mcp_tool_fail(JsonObject& result, String& err, int code, const char* msg);

// Translate an mcp_control_dispatch() outcome into the tool result/err contract,
// standardizing the SUCCESS envelope as {"ok":true[,"message":<msg>]}. `msg` is
// copied (String) so a caller stack buffer is safe once the handler returns.
// Returns true on success; on BUSY/TIMEOUT/handler-failure it sets err + the
// JSON-RPC error code via mcp_tool_fail and returns false.
//
// Tools whose deferred context stashes a heap pointer they must free on BUSY
// call mcp_control_dispatch() directly (to free before returning), then finish
// via this function for the OK/TIMEOUT/!ok cases.
bool mcp_finish_control(McpControlResult r, bool ok, const char* msg,
                        JsonObject& result, String& err);

// Convenience for the common control path: package `ctx`, defer `exec` to the
// main loop with a bounded wait, and finish via mcp_finish_control(). Use when
// `ctx` is self-contained (no heap buffer the caller must free on BUSY).
bool mcp_run_control(McpControlExec exec, const void* ctx, size_t ctx_len,
                     uint32_t timeout_ms, JsonObject& result, String& err);

// PSRAM-preferred allocation with an internal-RAM fallback (the pattern the pad
// / config / control tools repeat for PadConfig and JSON buffers). Returns
// nullptr only when both heaps are exhausted. Free with heap_caps_free / free.
void* mcp_psram_alloc(size_t size);

#endif // HAS_MCP
#endif // MCP_TOOL_UTIL_H

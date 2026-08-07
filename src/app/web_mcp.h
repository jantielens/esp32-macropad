#ifndef WEB_MCP_H
#define WEB_MCP_H

#include <ESPAsyncWebServer.h>
#include <stddef.h>

// ============================================================================
// MCP (Model Context Protocol) server — single POST /mcp endpoint.
//
// JSON-RPC 2.0 over the MCP Streamable HTTP transport (protocol 2025-06-18),
// JSON-only responses, stateless. Self-registers through the REGISTER_ROUTES()
// seam (web_portal_routes.h). Off by default; served in STA mode only behind a
// dedicated bearer token. Control tools are additionally gated by the global
// mcp_control_enabled toggle.
// ============================================================================

// Register the /mcp route. Invoked via REGISTER_ROUTES(web_mcp_register).
void web_mcp_register(AsyncWebServer* server);

// Main-loop hook: drains the deferred control job, fires a requested reboot
// after its grace period, and cleans up a stale request body. Call from
// web_portal_handle() (which runs on the main loop()).
void web_mcp_loop();

// Generate a new MCP bearer token into `out` (needs >= 33 bytes for 32 hex
// chars + NUL). Uses the hardware RNG (esp_fill_random); returns the token
// length written, or 0 on error.
size_t mcp_token_generate(char* out, size_t out_len);

// --- Deferred control bridge (web task -> main loop) -----------------------
//
// The deferred bridge is now generic (main_loop_bridge). These aliases keep the
// existing MCP tool call sites (mcp_control_dispatch / McpControlResult /
// McpControlExec / MCP_CONTROL_*) working unchanged. Control tools must never
// call action_dispatch, LVGL, or blocking I/O on the AsyncWebServer task; they
// package a job and dispatch it to run on the main loop. Single in-flight: a
// second concurrent request returns MCP_CONTROL_BUSY.
#include "main_loop_bridge.h"

typedef LoopBridgeExec McpControlExec;

enum McpControlResult {
    MCP_CONTROL_OK      = LOOP_BRIDGE_OK,       // exec ran; *out_ok / out_msg populated
    MCP_CONTROL_BUSY    = LOOP_BRIDGE_BUSY,     // another job is in flight
    MCP_CONTROL_TIMEOUT = LOOP_BRIDGE_TIMEOUT,  // exec did not complete within timeout
    MCP_CONTROL_INVALID = LOOP_BRIDGE_INVALID,
    MCP_CONTROL_UNAVAILABLE = LOOP_BRIDGE_UNAVAILABLE,
    MCP_CONTROL_TOO_LARGE = LOOP_BRIDGE_TOO_LARGE,
};

#define MCP_CONTROL_CTX_BYTES LOOP_BRIDGE_CTX_BYTES

static inline McpControlResult mcp_control_dispatch(McpControlExec exec,
                                                    const void* ctx, size_t ctx_len,
                                                    uint32_t timeout_ms,
                                                    bool* out_ok,
                                                    char* out_msg, size_t out_msg_len) {
    return (McpControlResult)loop_bridge_dispatch(exec, ctx, ctx_len, timeout_ms,
                                                  out_ok, out_msg, out_msg_len);
}

// Request a deferred device reboot. The restart fires from web_mcp_loop() after
// a short grace period so the in-flight JSON-RPC response flushes first.
void mcp_request_reboot();

#endif // WEB_MCP_H

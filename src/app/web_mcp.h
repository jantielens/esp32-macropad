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
// Control tools must never call action_dispatch, LVGL, the display manager, or
// blocking I/O on the AsyncWebServer (web) task. Instead they package a job and
// call mcp_control_dispatch(), which copies the context into an internal slot,
// schedules exec() to run on the main loop in web_mcp_loop(), and blocks
// (bounded) until it completes. Single in-flight: a second concurrent request
// returns MCP_CONTROL_BUSY.

enum McpControlResult {
    MCP_CONTROL_OK = 0,    // exec ran; *out_ok / out_msg are populated
    MCP_CONTROL_BUSY,      // another control job is in flight
    MCP_CONTROL_TIMEOUT,   // exec did not complete within timeout
};

// Job executed on the main loop. `ctx` points at the internal copy of the
// context bytes. The job writes success into *ok and an optional short message
// into msg (capacity msg_len). It runs in main/LVGL task context, so
// action_dispatch / display calls are safe here.
typedef void (*McpControlExec)(const void* ctx, bool* ok, char* msg, size_t msg_len);

// Maximum context size copied into the internal control slot.
#define MCP_CONTROL_CTX_BYTES 256

McpControlResult mcp_control_dispatch(McpControlExec exec,
                                      const void* ctx, size_t ctx_len,
                                      uint32_t timeout_ms,
                                      bool* out_ok,
                                      char* out_msg, size_t out_msg_len);

// Request a deferred device reboot. The restart fires from web_mcp_loop() after
// a short grace period so the in-flight JSON-RPC response flushes first.
void mcp_request_reboot();

#endif // WEB_MCP_H

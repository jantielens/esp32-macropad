#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Deferred main-loop bridge (web/async task -> main loop)
//
// Async web-server request handlers run on the AsyncTCP task, which must never
// call action_dispatch, LVGL, the display manager, or blocking I/O. When a
// handler needs to do main/LVGL-task work it packages a job and calls
// loop_bridge_dispatch(): the context bytes are copied into an internal slot,
// exec() is scheduled to run from the main loop (loop_bridge_drain()), and the
// caller blocks (bounded) until it completes. Single in-flight — a second
// concurrent request returns LOOP_BRIDGE_BUSY.
//
// This bridge is transport-agnostic: both the MCP server (web_mcp) and the web
// portal REST handlers use it. It is always compiled (independent of HAS_MCP)
// so the portal can defer work even when MCP is disabled.
// ============================================================================

enum LoopBridgeResult {
    LOOP_BRIDGE_OK = 0,    // exec ran; *out_ok / out_msg are populated
    LOOP_BRIDGE_BUSY,      // another job is in flight
    LOOP_BRIDGE_TIMEOUT,   // exec did not complete within timeout
    LOOP_BRIDGE_INVALID,   // invalid callback/context or consumer/ISR caller
    LOOP_BRIDGE_UNAVAILABLE, // bridge has not been initialized
    LOOP_BRIDGE_TOO_LARGE, // context exceeds LOOP_BRIDGE_CTX_BYTES
};

// Job executed on the main loop. `ctx` points at the internal copy of the
// context bytes. The job writes success into *ok and an optional short message
// into msg (capacity msg_len). It runs in main/LVGL task context, so
// action_dispatch / display calls are safe here.
typedef void (*LoopBridgeExec)(const void* ctx, bool* ok, char* msg, size_t msg_len);
typedef void (*LoopBridgeCleanup)(const void* ctx);

// Maximum context size copied into the internal slot.
#define LOOP_BRIDGE_CTX_BYTES 256

// Create the completion semaphore. Call once during setup (idempotent; safe to
// call again). loop_bridge_dispatch() returns LOOP_BRIDGE_UNAVAILABLE until
// this has run.
void loop_bridge_init();

// Web task: package a job, schedule it on the main loop, and block (bounded)
// until it completes or the timeout elapses.
LoopBridgeResult loop_bridge_dispatch(LoopBridgeExec exec,
                                      const void* ctx, size_t ctx_len,
                                      uint32_t timeout_ms,
                                      bool* out_ok,
                                      char* out_msg, size_t out_msg_len,
                                      LoopBridgeCleanup abandoned_cleanup = nullptr);

// Web task: queue a fire-and-forget job and return immediately. The main loop
// reclaims the slot after execution; callers only learn whether it was queued.
LoopBridgeResult loop_bridge_enqueue(LoopBridgeExec exec,
                                     const void* ctx, size_t ctx_len);

// Main loop: run a pending job if one is queued (call every loop iteration).
// No-op when nothing is pending.
void loop_bridge_drain();

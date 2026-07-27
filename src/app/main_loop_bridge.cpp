// ============================================================================
// main_loop_bridge.cpp — deferred web-task -> main-loop job bridge.
//
// Extracted from web_mcp.cpp so both the MCP server and the web portal REST
// handlers can defer work to the main loop through one shared, transport-
// agnostic slot. Compiled directly (sketch root); not gated on HAS_MCP.
// ============================================================================

#include "main_loop_bridge.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string.h>

static portMUX_TYPE      s_ctrl_mux      = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_ctrl_done_sem = nullptr;
static volatile bool     s_ctrl_busy     = false;   // slot occupied
static volatile bool     s_ctrl_pending  = false;   // queued, not yet run
static volatile bool     s_ctrl_done     = false;   // job finished
static volatile bool     s_ctrl_waiter   = false;   // a waiter is still blocked
static LoopBridgeExec    s_ctrl_exec     = nullptr;
static uint8_t           s_ctrl_ctx[LOOP_BRIDGE_CTX_BYTES];
static size_t            s_ctrl_ctx_len  = 0;
static bool              s_ctrl_ok       = false;
static char              s_ctrl_msg[160] = {0};

void loop_bridge_init() {
    if (!s_ctrl_done_sem) {
        s_ctrl_done_sem = xSemaphoreCreateBinary();
    }
}

LoopBridgeResult loop_bridge_enqueue(LoopBridgeExec exec,
                                     const void* ctx, size_t ctx_len) {
    if (!exec || !s_ctrl_done_sem) return LOOP_BRIDGE_TIMEOUT;
    if (ctx_len > sizeof(s_ctrl_ctx)) ctx_len = sizeof(s_ctrl_ctx);

    portENTER_CRITICAL(&s_ctrl_mux);
    if (s_ctrl_busy) {
        portEXIT_CRITICAL(&s_ctrl_mux);
        return LOOP_BRIDGE_BUSY;
    }
    s_ctrl_busy    = true;
    s_ctrl_pending = true;
    s_ctrl_done    = false;
    s_ctrl_waiter  = false;
    s_ctrl_exec    = exec;
    s_ctrl_ctx_len = ctx_len;
    if (ctx && ctx_len) memcpy(s_ctrl_ctx, ctx, ctx_len);
    s_ctrl_ok      = false;
    s_ctrl_msg[0]  = '\0';
    portEXIT_CRITICAL(&s_ctrl_mux);
    return LOOP_BRIDGE_OK;
}

LoopBridgeResult loop_bridge_dispatch(LoopBridgeExec exec,
                                      const void* ctx, size_t ctx_len,
                                      uint32_t timeout_ms,
                                      bool* out_ok,
                                      char* out_msg, size_t out_msg_len) {
    if (!exec || !s_ctrl_done_sem) return LOOP_BRIDGE_TIMEOUT;
    if (ctx_len > sizeof(s_ctrl_ctx)) ctx_len = sizeof(s_ctrl_ctx);

    portENTER_CRITICAL(&s_ctrl_mux);
    if (s_ctrl_busy) {
        portEXIT_CRITICAL(&s_ctrl_mux);
        return LOOP_BRIDGE_BUSY;
    }
    s_ctrl_busy    = true;
    s_ctrl_pending = true;
    s_ctrl_done    = false;
    s_ctrl_waiter  = true;
    s_ctrl_exec    = exec;
    s_ctrl_ctx_len = ctx_len;
    if (ctx && ctx_len) memcpy(s_ctrl_ctx, ctx, ctx_len);
    s_ctrl_ok      = false;
    s_ctrl_msg[0]  = '\0';
    portEXIT_CRITICAL(&s_ctrl_mux);

    // Defensive: drain any stale completion signal before waiting.
    xSemaphoreTake(s_ctrl_done_sem, 0);

    if (xSemaphoreTake(s_ctrl_done_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        // Job completed and notified us.
        portENTER_CRITICAL(&s_ctrl_mux);
        if (out_ok) *out_ok = s_ctrl_ok;
        if (out_msg && out_msg_len) strlcpy(out_msg, s_ctrl_msg, out_msg_len);
        s_ctrl_busy   = false;
        s_ctrl_waiter = false;
        portEXIT_CRITICAL(&s_ctrl_mux);
        return LOOP_BRIDGE_OK;
    }

    // Timed out waiting. Re-check whether the job actually completed at the
    // boundary; if so consume its signal and report OK. Otherwise abandon the
    // slot — the main loop will reclaim it when the job finishes.
    portENTER_CRITICAL(&s_ctrl_mux);
    if (s_ctrl_done) {
        portEXIT_CRITICAL(&s_ctrl_mux);
        xSemaphoreTake(s_ctrl_done_sem, 0);
        portENTER_CRITICAL(&s_ctrl_mux);
        if (out_ok) *out_ok = s_ctrl_ok;
        if (out_msg && out_msg_len) strlcpy(out_msg, s_ctrl_msg, out_msg_len);
        s_ctrl_busy   = false;
        s_ctrl_waiter = false;
        portEXIT_CRITICAL(&s_ctrl_mux);
        return LOOP_BRIDGE_OK;
    }
    s_ctrl_waiter = false;  // job still pending/running; leave busy for reclaim
    portEXIT_CRITICAL(&s_ctrl_mux);
    return LOOP_BRIDGE_TIMEOUT;
}

void loop_bridge_drain() {
    bool run = false;
    LoopBridgeExec exec = nullptr;
    portENTER_CRITICAL(&s_ctrl_mux);
    if (s_ctrl_pending) {
        s_ctrl_pending = false;
        exec = s_ctrl_exec;
        run = true;
    }
    portEXIT_CRITICAL(&s_ctrl_mux);

    if (run && exec) {
        bool ok = false;
        char msg[160];
        msg[0] = '\0';
        exec(s_ctrl_ctx, &ok, msg, sizeof(msg));

        portENTER_CRITICAL(&s_ctrl_mux);
        s_ctrl_ok = ok;
        strlcpy(s_ctrl_msg, msg, sizeof(s_ctrl_msg));
        s_ctrl_done = true;
        const bool notify = s_ctrl_waiter;
        if (!notify) {
            // Waiter abandoned (timed out). Reclaim the slot now.
            s_ctrl_busy = false;
        }
        portEXIT_CRITICAL(&s_ctrl_mux);
        if (notify) xSemaphoreGive(s_ctrl_done_sem);
    }
}

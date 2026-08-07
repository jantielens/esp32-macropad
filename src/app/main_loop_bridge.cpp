// ============================================================================
// main_loop_bridge.cpp — deferred web-task -> main-loop job bridge.
//
// Extracted from web_mcp.cpp so both the MCP server and the web portal REST
// handlers can defer work to the main loop through one shared, transport-
// agnostic slot. Compiled directly (sketch root); not gated on HAS_MCP.
// ============================================================================

#include "main_loop_bridge.h"

#include "deferred_dispatch_slot.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static DeferredDispatchSlot<LOOP_BRIDGE_CTX_BYTES> s_ctrl_slot;
static TaskHandle_t s_ctrl_consumer_task = nullptr;

static LoopBridgeResult bridge_result(DeferredDispatchResult result) {
    return static_cast<LoopBridgeResult>(result);
}

void loop_bridge_init() {
    if (!s_ctrl_consumer_task) s_ctrl_consumer_task = xTaskGetCurrentTaskHandle();
    s_ctrl_slot.init();
}

LoopBridgeResult loop_bridge_enqueue(LoopBridgeExec exec,
                                     const void* ctx, size_t ctx_len) {
    return bridge_result(s_ctrl_slot.enqueue(exec, ctx, ctx_len, xPortInIsrContext()));
}

LoopBridgeResult loop_bridge_dispatch(LoopBridgeExec exec,
                                      const void* ctx, size_t ctx_len,
                                      uint32_t timeout_ms,
                                      bool* out_ok,
                                      char* out_msg, size_t out_msg_len,
                                      LoopBridgeCleanup abandoned_cleanup) {
    const bool caller_is_consumer = s_ctrl_consumer_task &&
                                    xTaskGetCurrentTaskHandle() == s_ctrl_consumer_task;
    return bridge_result(s_ctrl_slot.dispatch(
        exec, abandoned_cleanup, ctx, ctx_len, timeout_ms,
        caller_is_consumer, xPortInIsrContext(), out_ok, out_msg, out_msg_len));
}

void loop_bridge_drain() {
    s_ctrl_slot.drain();
}

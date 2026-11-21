#pragma once

#include <stddef.h>
#include "../../src/app/deferred_dispatch_slot.h"

#define DISPLAY_MANAGER_H
using DisplayTaskExec = DeferredDispatchExec;
using DisplayTaskCleanup = DeferredDispatchCleanup;
using DisplayTaskDispatchResult = DeferredDispatchResult;
static constexpr auto DISPLAY_TASK_DISPATCH_OK = DEFERRED_DISPATCH_OK;
static constexpr auto DISPLAY_TASK_DISPATCH_BUSY = DEFERRED_DISPATCH_BUSY;
static constexpr auto DISPLAY_TASK_DISPATCH_TIMEOUT = DEFERRED_DISPATCH_TIMEOUT;
static constexpr size_t DISPLAY_TASK_DISPATCH_CTX_BYTES = 64;
DisplayTaskDispatchResult display_manager_dispatch(
    DisplayTaskExec exec, DisplayTaskCleanup cleanup, const void* ctx, size_t ctx_len,
    uint32_t timeout_ms, bool* out_ok, char* out_msg, size_t out_msg_len);

class String {
public:
    String(const char* value) : value_(value ? value : "") {}
    const char* c_str() const { return value_; }

private:
    const char* value_;
};
#pragma once

#include <stdint.h>

class ItemBindingScope;

struct ThreadExecutionContext {
    bool (*work_guard)(uint32_t) = nullptr;
    uint32_t generation = 0;
    ItemBindingScope* item_scope = nullptr;
    bool synchronous = false;
    bool alarm_audio = false;
    bool interactive = false;
};

#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(ThreadExecutionContext) == 16, "Per-task context exceeds the 16-byte TLS budget");
#endif

inline thread_local ThreadExecutionContext thread_execution_context;

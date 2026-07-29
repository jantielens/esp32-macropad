#pragma once

#include "ha_service.h"

#if HAS_MCP && (HAS_DISPLAY || HAS_BUTTON)

enum McpPressDispatchResult : uint8_t {
    MCP_PRESS_DISPATCH_COMPLETED = 0,
    MCP_PRESS_DISPATCH_TRACKED,
    MCP_PRESS_DISPATCH_BUSY,
    MCP_PRESS_DISPATCH_INVALID,
};

struct McpPressDispatchOps {
    bool (*reserve)(uint8_t action_count, uint32_t* execution_id, void* context);
    void (*dispatch_list)(const ButtonAction* actions, uint8_t count, void* context);
    void (*dispatch_action)(const ButtonAction& action, void* context);
    void (*set_action)(uint32_t execution_id, uint8_t result_index,
                       uint8_t action_index, const HaServicePayload& payload,
                       void* context);
    HaServiceEnqueueResult (*enqueue)(const HaServicePayload& payload,
                                     uint32_t execution_id,
                                     uint8_t action_index, void* context);
    void (*record)(const HaServiceResult& result, void* context);
};

McpPressDispatchResult mcp_press_button_dispatch(
    const ButtonAction* actions, uint8_t count,
    const McpPressDispatchOps& ops, void* context,
    uint32_t* execution_id);

#endif
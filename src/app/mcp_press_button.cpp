#include "mcp_press_button.h"

#if HAS_MCP && (HAS_DISPLAY || HAS_BUTTON)

#include <string.h>

McpPressDispatchResult mcp_press_button_dispatch(
    const ButtonAction* actions, uint8_t count,
    const McpPressDispatchOps& ops, void* context,
    uint32_t* execution_id) {
    if (count > MAX_BUTTON_ACTIONS) return MCP_PRESS_DISPATCH_INVALID;
    if (!actions || !count) return MCP_PRESS_DISPATCH_COMPLETED;

    uint8_t ha_action_count = 0;
    for (uint8_t action_index = 0; action_index < count; action_index++) {
        if (strcmp(actions[action_index].type, ACTION_TYPE_HA_SERVICE) == 0) {
            ha_action_count++;
        }
    }

    if (!ha_action_count) {
        ops.dispatch_list(actions, count, context);
        return MCP_PRESS_DISPATCH_COMPLETED;
    }

    uint32_t reserved_id = 0;
    if (!ops.reserve(ha_action_count, &reserved_id, context)) {
        return MCP_PRESS_DISPATCH_BUSY;
    }
    if (execution_id) *execution_id = reserved_id;

    uint8_t result_index = 0;
    for (uint8_t action_index = 0; action_index < count; action_index++) {
        const ButtonAction& action = actions[action_index];
        if (strcmp(action.type, ACTION_TYPE_HA_SERVICE) != 0) {
            ops.dispatch_action(action, context);
            continue;
        }

        const HaServicePayload& payload = action.payload.ha_service;
        ops.set_action(reserved_id, result_index, action_index, payload, context);
        if (ops.enqueue(payload, reserved_id, action_index, context) ==
            HA_SERVICE_QUEUE_FULL) {
            HaServiceResult rejected = {};
            rejected.execution_id = reserved_id;
            rejected.action_index = action_index;
            rejected.status = HA_STATUS_QUEUE_FULL;
            rejected.http_status = HA_HTTP_STATUS_NONE;
            strncpy(rejected.entity_id, payload.entity_id,
                    sizeof(rejected.entity_id) - 1);
            strncpy(rejected.service, payload.service,
                    sizeof(rejected.service) - 1);
            ops.record(rejected, context);
        }
        result_index++;
    }
    return MCP_PRESS_DISPATCH_TRACKED;
}

#endif
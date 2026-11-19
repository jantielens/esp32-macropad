#include "mcp_press_button.h"

#if HAS_MCP && (HAS_DISPLAY || HAS_BUTTON)

#include "action_registry.h"
#include "action_storage.h"
#include <new>
#include <string.h>

namespace {
bool is_ha_action(const ButtonAction& action) {
    const ActionOperationDef* operation = action.payload.operation.descriptor;
    return operation && operation->id && !strcmp(operation->id, "ha.service.call");
}

struct TrackedPress {
    McpPressDispatchOps ops;
    void* context;
    uint32_t execution_id;
    uint8_t count;
    uint8_t indices[MAX_BUTTON_ACTIONS];
    bool started[MAX_BUTTON_ACTIONS];
};

int tracked_index(const TrackedPress& press, uint8_t action_index) {
    for (uint8_t index = 0; index < press.count; ++index)
        if (press.indices[index] == action_index) return index;
    return -1;
}

void record_result(TrackedPress& press, uint8_t index, HaServiceStatus status,
                   const HaServicePayload* payload = nullptr) {
    HaServiceResult result = {};
    result.execution_id = press.execution_id;
    result.action_index = press.indices[index];
    result.status = status;
    result.http_status = HA_HTTP_STATUS_NONE;
    if (payload) {
        strlcpy(result.entity_id, payload->entity_id, sizeof(result.entity_id));
        strlcpy(result.service, payload->service, sizeof(result.service));
    }
    press.started[index] = true;
    press.ops.record(result, press.context);
}

bool tracked_dispatch_ha(void* data, uint8_t action_index, const HaServicePayload& payload,
                         ActionResult* result) {
    TrackedPress& press = *static_cast<TrackedPress*>(data);
    const int index = tracked_index(press, action_index);
    if (index < 0 || !result) return false;
    press.ops.set_action(press.execution_id, index, action_index, payload, press.context);
    if (press.ops.enqueue(payload, press.execution_id, action_index, press.context) ==
        HA_SERVICE_QUEUE_FULL) {
        record_result(press, index, HA_STATUS_QUEUE_FULL, &payload);
        *result = ACTION_FAILED;
    } else {
        press.started[index] = true;
        *result = ACTION_COMPLETE;
    }
    return true;
}

void tracked_failed(void* data, uint8_t action_index) {
    TrackedPress& press = *static_cast<TrackedPress*>(data);
    const int index = tracked_index(press, action_index);
    if (index >= 0 && !press.started[index])
        record_result(press, index, HA_STATUS_INVALID_REQUEST);
}

void tracked_release(void* data) {
    TrackedPress* press = static_cast<TrackedPress*>(data);
    for (uint8_t index = 0; index < press->count; ++index)
        if (!press->started[index]) record_result(*press, index, HA_STATUS_CANCELLED);
    press->~TrackedPress();
    action_storage_free(press);
}
}

McpPressDispatchResult mcp_press_button_dispatch(
    const ButtonAction* actions, uint8_t count,
    const McpPressDispatchOps& ops, void* context,
    uint32_t* execution_id, void* observer_context) {
    if (count > MAX_BUTTON_ACTIONS) return MCP_PRESS_DISPATCH_INVALID;
    if (!count) return MCP_PRESS_DISPATCH_COMPLETED;
    if (!actions) return MCP_PRESS_DISPATCH_INVALID;

    uint8_t ha_action_count = 0;
    for (uint8_t index = 0; index < count; ++index) {
        const ButtonAction& action = actions[index];
        if (action.type[0] != '@' || action.type[1] ||
            !action.payload.operation.descriptor || !action.payload.operation.descriptor->id)
            return MCP_PRESS_DISPATCH_INVALID;
        if (!is_ha_action(actions[index])) continue;
        ++ha_action_count;
    }
    if (ha_action_count) {
        if (!ops.dispatch_observed) return MCP_PRESS_DISPATCH_INVALID;
        void* memory = action_storage_allocate(sizeof(TrackedPress));
        if (!memory) return MCP_PRESS_DISPATCH_BUSY;
        TrackedPress* press = new (memory) TrackedPress{};
        press->ops = ops;
        press->context = observer_context;
        press->count = ha_action_count;
        if (!ops.reserve(ha_action_count, &press->execution_id, context)) {
            press->~TrackedPress();
            action_storage_free(press);
            return MCP_PRESS_DISPATCH_BUSY;
        }
        if (execution_id) *execution_id = press->execution_id;
        uint8_t result_index = 0;
        for (uint8_t index = 0; index < count; ++index) {
            if (!is_ha_action(actions[index])) continue;
            press->indices[result_index] = index;
            const HaServicePayload empty = {};
            ops.set_action(press->execution_id, result_index++, index, empty, context);
        }
        const ActionRunObserver observer = {
            press, tracked_dispatch_ha, tracked_failed, tracked_release
        };
        ops.dispatch_observed(actions, count, observer, context);
        return MCP_PRESS_DISPATCH_TRACKED;
    }
    ops.dispatch_list(actions, count, context);
    return MCP_PRESS_DISPATCH_COMPLETED;
}

#endif
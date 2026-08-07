#include "mcp_press_button.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum EventType : uint8_t {
    EVENT_RESERVE,
    EVENT_DISPATCH_LIST,
    EVENT_DISPATCH_ACTION,
    EVENT_SET_ACTION,
    EVENT_ENQUEUE,
    EVENT_RECORD,
};

struct Event {
    EventType type;
    uint8_t action_index;
    uint8_t result_index;
};

struct Harness {
    Event events[16] = {};
    uint8_t event_count = 0;
    bool reserve_ok = true;
    HaServiceEnqueueResult enqueue_results[MAX_BUTTON_ACTIONS] = {};
    uint8_t enqueue_count = 0;
    HaServiceResult recorded = {};
};

static void add_event(Harness* harness, EventType type,
                      uint8_t action_index = 0,
                      uint8_t result_index = 0) {
    harness->events[harness->event_count++] = {type, action_index, result_index};
}

static bool reserve(uint8_t action_count, uint32_t* execution_id, void* context) {
    Harness* harness = static_cast<Harness*>(context);
    add_event(harness, EVENT_RESERVE, action_count);
    if (harness->reserve_ok) *execution_id = 77;
    return harness->reserve_ok;
}

static void dispatch_list(const ButtonAction*, uint8_t count, void* context) {
    add_event(static_cast<Harness*>(context), EVENT_DISPATCH_LIST, count);
}

static void dispatch_action(const ButtonAction& action, void* context) {
    add_event(static_cast<Harness*>(context), EVENT_DISPATCH_ACTION,
              (uint8_t)atoi(action.payload.system.system_command));
}

static void set_action(uint32_t execution_id, uint8_t result_index,
                       uint8_t action_index, const HaServicePayload&,
                       void* context) {
    assert(execution_id == 77);
    add_event(static_cast<Harness*>(context), EVENT_SET_ACTION,
              action_index, result_index);
}

static HaServiceEnqueueResult enqueue(const HaServicePayload&, uint32_t execution_id,
                                      uint8_t action_index, void* context) {
    assert(execution_id == 77);
    Harness* harness = static_cast<Harness*>(context);
    add_event(harness, EVENT_ENQUEUE, action_index);
    return harness->enqueue_results[harness->enqueue_count++];
}

static void record(const HaServiceResult& result, void* context) {
    Harness* harness = static_cast<Harness*>(context);
    add_event(harness, EVENT_RECORD, result.action_index);
    harness->recorded = result;
}

static const McpPressDispatchOps OPS = {
    reserve, dispatch_list, dispatch_action, set_action, enqueue, record,
};

static ButtonAction system_action(uint8_t marker) {
    ButtonAction action = {};
    strcpy(action.type, ACTION_TYPE_SYSTEM);
    snprintf(action.payload.system.system_command,
             sizeof(action.payload.system.system_command), "%u", marker);
    return action;
}

static ButtonAction ha_action(const char* entity, const char* service) {
    ButtonAction action = {};
    strcpy(action.type, ACTION_TYPE_HA_SERVICE);
    strncpy(action.payload.ha_service.entity_id, entity,
            sizeof(action.payload.ha_service.entity_id) - 1);
    strncpy(action.payload.ha_service.service, service,
            sizeof(action.payload.ha_service.service) - 1);
    return action;
}

static void test_no_ha_uses_unchanged_list_dispatch() {
    Harness harness;
    ButtonAction actions[] = {system_action(1), system_action(2)};
    uint32_t execution_id = 0;
    assert(mcp_press_button_dispatch(actions, 2, OPS, &harness, &execution_id) ==
           MCP_PRESS_DISPATCH_COMPLETED);
    assert(execution_id == 0);
    assert(harness.event_count == 1);
    assert(harness.events[0].type == EVENT_DISPATCH_LIST);
    assert(harness.events[0].action_index == 2);
}

static void test_action_count_boundaries() {
    Harness harness;
    uint32_t execution_id = 0;
    assert(mcp_press_button_dispatch(nullptr, 0, OPS, &harness, &execution_id) ==
           MCP_PRESS_DISPATCH_COMPLETED);
    assert(harness.event_count == 0);

    ButtonAction maximum[MAX_BUTTON_ACTIONS] = {
        system_action(0), system_action(1), system_action(2),
    };
    assert(mcp_press_button_dispatch(maximum, MAX_BUTTON_ACTIONS, OPS,
                                     &harness, &execution_id) ==
           MCP_PRESS_DISPATCH_COMPLETED);
    assert(harness.event_count == 1);
    assert(harness.events[0].type == EVENT_DISPATCH_LIST);
    assert(harness.events[0].action_index == MAX_BUTTON_ACTIONS);

    ButtonAction oversized[MAX_BUTTON_ACTIONS + 1] = {};
    harness.event_count = 0;
    assert(mcp_press_button_dispatch(oversized, MAX_BUTTON_ACTIONS + 1, OPS,
                                     &harness, &execution_id) ==
           MCP_PRESS_DISPATCH_INVALID);
    assert(harness.event_count == 0);
}

static void test_reserve_precedes_mixed_side_effects() {
    Harness harness;
    ButtonAction actions[] = {
        ha_action("light.one", "turn_on"),
        system_action(1),
        ha_action("light.two", "turn_off"),
    };
    uint32_t execution_id = 0;
    assert(mcp_press_button_dispatch(actions, 3, OPS, &harness, &execution_id) ==
           MCP_PRESS_DISPATCH_TRACKED);
    assert(execution_id == 77);
    const Event expected[] = {
        {EVENT_RESERVE, 2, 0},
        {EVENT_SET_ACTION, 0, 0}, {EVENT_ENQUEUE, 0, 0},
        {EVENT_DISPATCH_ACTION, 1, 0},
        {EVENT_SET_ACTION, 2, 1}, {EVENT_ENQUEUE, 2, 0},
    };
    assert(harness.event_count == sizeof(expected) / sizeof(expected[0]));
    assert(memcmp(harness.events, expected, sizeof(expected)) == 0);
}

static void test_queue_full_records_terminal_result() {
    Harness harness;
    harness.enqueue_results[0] = HA_SERVICE_QUEUE_FULL;
    ButtonAction action = ha_action("light.full", "toggle");
    uint32_t execution_id = 0;
    assert(mcp_press_button_dispatch(&action, 1, OPS, &harness, &execution_id) ==
           MCP_PRESS_DISPATCH_TRACKED);
    assert(harness.event_count == 4);
    assert(harness.events[3].type == EVENT_RECORD);
    assert(harness.recorded.execution_id == 77);
    assert(harness.recorded.action_index == 0);
    assert(harness.recorded.status == HA_STATUS_QUEUE_FULL);
    assert(harness.recorded.http_status == HA_HTTP_STATUS_NONE);
    assert(strcmp(harness.recorded.entity_id, "light.full") == 0);
    assert(strcmp(harness.recorded.service, "toggle") == 0);
}

static void test_busy_rejects_before_side_effects() {
    Harness harness;
    harness.reserve_ok = false;
    ButtonAction actions[] = {system_action(0), ha_action("light.busy", "toggle")};
    uint32_t execution_id = 0;
    assert(mcp_press_button_dispatch(actions, 2, OPS, &harness, &execution_id) ==
           MCP_PRESS_DISPATCH_BUSY);
    assert(execution_id == 0);
    assert(harness.event_count == 1);
    assert(harness.events[0].type == EVENT_RESERVE);
}

int main() {
    test_no_ha_uses_unchanged_list_dispatch();
    test_action_count_boundaries();
    test_reserve_precedes_mixed_side_effects();
    test_queue_full_records_terminal_result();
    test_busy_rejects_before_side_effects();
    puts("All MCP press-button orchestration tests passed");
    return 0;
}
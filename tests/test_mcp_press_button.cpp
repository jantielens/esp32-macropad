#include "mcp_press_button.h"
#include "action_registry.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool allocation_fails = false;
void* action_storage_allocate(size_t size) { return allocation_fails ? nullptr : malloc(size); }
void action_storage_free(void* memory) { free(memory); }

enum EventType : uint8_t {
    EVENT_RESERVE,
    EVENT_DISPATCH_LIST,
    EVENT_SET_ACTION,
    EVENT_ENQUEUE,
    EVENT_RECORD,
    EVENT_DISPATCH_OBSERVED,
};

struct Event {
    EventType type;
    uint8_t action_index;
    uint8_t result_index;
};

struct Harness {
    Event events[16] = {};
    uint8_t event_count = 0;
    const ButtonAction* list_actions = nullptr;
    bool reserve_ok = true;
    HaServiceEnqueueResult enqueue_results[MAX_BUTTON_ACTIONS] = {};
    uint8_t enqueue_count = 0;
    HaServiceResult recorded = {};
    HaServiceResult records[MAX_BUTTON_ACTIONS] = {};
    uint8_t record_count = 0;
    HaServicePayload enqueued[MAX_BUTTON_ACTIONS] = {};
    ActionRunObserver observer = {};
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

static void dispatch_list(const ButtonAction* actions, uint8_t count, void* context) {
    Harness* harness = static_cast<Harness*>(context);
    harness->list_actions = actions;
    add_event(harness, EVENT_DISPATCH_LIST, count);
}

static void set_action(uint32_t execution_id, uint8_t result_index,
                       uint8_t action_index, const HaServicePayload&,
                       void* context) {
    assert(execution_id == 77);
    add_event(static_cast<Harness*>(context), EVENT_SET_ACTION,
              action_index, result_index);
}

static HaServiceEnqueueResult enqueue(const HaServicePayload& payload, uint32_t execution_id,
                                      uint8_t action_index, void* context) {
    assert(execution_id == 77);
    Harness* harness = static_cast<Harness*>(context);
    add_event(harness, EVENT_ENQUEUE, action_index);
    harness->enqueued[harness->enqueue_count] = payload;
    return harness->enqueue_results[harness->enqueue_count++];
}

static void record(const HaServiceResult& result, void* context) {
    Harness* harness = static_cast<Harness*>(context);
    add_event(harness, EVENT_RECORD, result.action_index);
    harness->recorded = result;
    harness->records[harness->record_count++] = result;
}

static void dispatch_observed(const ButtonAction*, uint8_t count,
                              const ActionRunObserver& observer, void* context) {
    Harness* harness = static_cast<Harness*>(context);
    harness->observer = observer;
    add_event(harness, EVENT_DISPATCH_OBSERVED, count);
}

static const McpPressDispatchOps OPS = {
    reserve, dispatch_list, set_action, enqueue, record, dispatch_observed,
};

static ButtonAction typed_action(const ActionOperationDef* operation) {
    ButtonAction action = {};
    action.type[0] = '@';
    action.payload.operation.descriptor = operation;
    return action;
}

static ButtonAction system_action(uint8_t) {
    static const ActionOperationDef system = {"system.reboot", "Reboot", nullptr, 0, nullptr};
    return typed_action(&system);
}

static void test_typed_ha_tracking_and_cancellation() {
    const ActionOperationDef ha = {"ha.service.call", "Call service", nullptr, 0, nullptr};
    Harness harness;
    ButtonAction actions[] = {system_action(0), typed_action(&ha), typed_action(&ha)};
    uint32_t execution_id = 0;
    assert(mcp_press_button_dispatch(actions, 3, OPS, &harness, &execution_id, &harness) ==
           MCP_PRESS_DISPATCH_TRACKED);
    assert(execution_id == 77 && harness.event_count == 4);
    assert(harness.events[0].type == EVENT_RESERVE && harness.events[0].action_index == 2);
    assert(harness.events[1].type == EVENT_SET_ACTION && harness.events[1].action_index == 1);
    assert(harness.events[2].type == EVENT_SET_ACTION && harness.events[2].action_index == 2);
    assert(harness.events[3].type == EVENT_DISPATCH_OBSERVED);

    HaServicePayload normalized = {};
    strcpy(normalized.entity_id, "light.resolved");
    strcpy(normalized.service, "turn_on");
    strcpy(normalized.data_json, "{\"brightness\":80}");
    ActionResult result = ACTION_FAILED;
    assert(harness.observer.dispatch_ha(harness.observer.data, 1, normalized, &result));
    assert(result == ACTION_COMPLETE && harness.enqueue_count == 1);
    assert(!strcmp(harness.enqueued[0].entity_id, "light.resolved"));
    assert(!strcmp(harness.enqueued[0].data_json, "{\"brightness\":80}"));
    harness.observer.release(harness.observer.data);
    assert(harness.record_count == 1);
    assert(harness.records[0].action_index == 2 &&
           harness.records[0].status == HA_STATUS_CANCELLED);
}

static void test_typed_ha_failure_and_busy() {
    const ActionOperationDef ha = {"ha.service.call", "Call service", nullptr, 0, nullptr};
    ButtonAction actions[] = {typed_action(&ha), typed_action(&ha)};
    Harness invalid;
    assert(mcp_press_button_dispatch(actions, 2, OPS, &invalid, nullptr, &invalid) ==
           MCP_PRESS_DISPATCH_TRACKED);
    invalid.observer.failed(invalid.observer.data, 0);
    invalid.observer.release(invalid.observer.data);
    assert(invalid.enqueue_count == 0 && invalid.record_count == 2);
    assert(invalid.records[0].status == HA_STATUS_INVALID_REQUEST &&
           invalid.records[0].action_index == 0);
    assert(invalid.records[1].status == HA_STATUS_CANCELLED &&
           invalid.records[1].action_index == 1);

    Harness full;
    full.enqueue_results[0] = HA_SERVICE_QUEUE_FULL;
    assert(mcp_press_button_dispatch(actions, 2, OPS, &full, nullptr, &full) ==
           MCP_PRESS_DISPATCH_TRACKED);
    HaServicePayload payload = {};
    strcpy(payload.entity_id, "light.full");
    strcpy(payload.service, "toggle");
    ActionResult result = ACTION_COMPLETE;
    assert(full.observer.dispatch_ha(full.observer.data, 0, payload, &result));
    assert(result == ACTION_FAILED);
    full.observer.failed(full.observer.data, 0);
    full.observer.release(full.observer.data);
    assert(full.record_count == 2 && full.records[0].status == HA_STATUS_QUEUE_FULL &&
           full.records[1].status == HA_STATUS_CANCELLED);

    Harness busy;
    busy.reserve_ok = false;
    assert(mcp_press_button_dispatch(actions, 2, OPS, &busy, nullptr) ==
           MCP_PRESS_DISPATCH_BUSY);
    assert(busy.event_count == 1 && busy.events[0].type == EVENT_RESERVE);

    Harness no_memory;
    allocation_fails = true;
    assert(mcp_press_button_dispatch(actions, 2, OPS, &no_memory, nullptr, &no_memory) ==
           MCP_PRESS_DISPATCH_BUSY);
    allocation_fails = false;
    assert(no_memory.event_count == 0);
}

static void test_typed_confirmation_uses_continuation_runner() {
    const ActionOperationDef confirm = {
        "dialog.confirm", "Ask for confirmation", nullptr, 0, nullptr, ACTION_EXECUTION_PAUSABLE
    };
    const ActionOperationDef ha = {"ha.service.call", "Call service", nullptr, 0, nullptr};
    for (bool confirmation_first : {true, false}) {
        Harness harness;
        ButtonAction actions[] = {
            confirmation_first ? typed_action(&confirm) : typed_action(&ha),
            confirmation_first ? typed_action(&ha) : typed_action(&confirm),
            system_action(2),
        };
        uint32_t execution_id = 0;
        assert(mcp_press_button_dispatch(actions, 3, OPS, &harness, &execution_id, &harness) ==
               MCP_PRESS_DISPATCH_TRACKED);
        assert(execution_id == 77 && harness.event_count == 3);
        assert(harness.events[2].type == EVENT_DISPATCH_OBSERVED);
        harness.observer.release(harness.observer.data);
        assert(harness.record_count == 1 && harness.records[0].status == HA_STATUS_CANCELLED);
    }

    Harness unknown;
    ButtonAction actions[] = {typed_action(nullptr), typed_action(&ha)};
    assert(mcp_press_button_dispatch(actions, 2, OPS, &unknown, nullptr) ==
           MCP_PRESS_DISPATCH_INVALID);
    assert(unknown.event_count == 0);
}

static void test_synchronous_typed_action_keeps_ha_tracking() {
    const ActionOperationDef sync = {"display.refresh_full", "Refresh", nullptr, 0, nullptr};
    const ActionOperationDef ha = {"ha.service.call", "Call service", nullptr, 0, nullptr};
    Harness harness;
    ButtonAction actions[] = {typed_action(&sync), typed_action(&ha)};
    uint32_t execution_id = 0;
    assert(mcp_press_button_dispatch(actions, 2, OPS, &harness, &execution_id, &harness) ==
           MCP_PRESS_DISPATCH_TRACKED);
    assert(execution_id == 77 && harness.event_count == 3);
    assert(harness.events[0].type == EVENT_RESERVE);
    assert(harness.events[1].type == EVENT_SET_ACTION);
    assert(harness.events[2].type == EVENT_DISPATCH_OBSERVED);
    harness.observer.release(harness.observer.data);
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

static void test_incompatible_actions_have_no_side_effects() {
    const ActionOperationDef ha = {"ha.service.call", "Call service", nullptr, 0, nullptr};
    Harness harness;
    ButtonAction legacy = {};
    strcpy(legacy.type, ACTION_TYPE_HA_SERVICE);
    ButtonAction actions[] = {typed_action(&ha), legacy};
    uint32_t execution_id = 0;
    assert(mcp_press_button_dispatch(actions, 2, OPS, &harness, &execution_id) ==
           MCP_PRESS_DISPATCH_INVALID);
    assert(execution_id == 0 && harness.event_count == 0);
    assert(mcp_press_button_dispatch(nullptr, 1, OPS, &harness, nullptr) ==
           MCP_PRESS_DISPATCH_INVALID);

    McpPressDispatchOps no_observer = OPS;
    no_observer.dispatch_observed = nullptr;
    ButtonAction typed = typed_action(&ha);
    assert(mcp_press_button_dispatch(&typed, 1, no_observer, &harness, nullptr) ==
           MCP_PRESS_DISPATCH_INVALID);
    assert(harness.event_count == 0);
}

int main() {
    test_no_ha_uses_unchanged_list_dispatch();
    test_typed_ha_tracking_and_cancellation();
    test_typed_ha_failure_and_busy();
    test_typed_confirmation_uses_continuation_runner();
    test_synchronous_typed_action_keeps_ha_tracking();
    test_action_count_boundaries();
    test_incompatible_actions_have_no_side_effects();
    puts("All MCP press-button orchestration tests passed");
    return 0;
}
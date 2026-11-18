#include "ha_service_delivery.h"

#include <ArduinoJson.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "action_registry.h"
#include "action_dispatch.h"
#include "action_validate.h"
#define ARDUINO 1
#include "../src/app/actions/ha_service_action.cpp"
#undef ARDUINO

static HaServiceDelivery operation_delivery;
static ActionDispatchContext operation_context;
static HaServicePayload immediate_payload;
static bool immediate_result = true;
static unsigned immediate_calls = 0;

void action_type_register(const ActionTypeDef*) {}
bool action_run_dispatch_ha(const HaServicePayload&, ActionResult*) { return false; }
ActionDispatchContext action_dispatch_context() { return operation_context; }
const char* action_validate_json(JsonObjectConst action, bool, const char*, const char*) {
    return validate_ha_service(action);
}
bool action_json_object_valid(const char* value) {
    JsonDocument document;
    return !deserializeJson(document, value) && document.is<JsonObject>();
}
HaServiceEnqueueResult ha_service_enqueue(const HaServicePayload& value, uint32_t execution_id,
                                          uint8_t action_index) {
    return operation_delivery.enqueue(value, execution_id, action_index);
}
bool ha_service_execute_immediate(const HaServicePayload& value) {
    immediate_payload = value;
    ++immediate_calls;
    return immediate_result;
}

static void test_operation_dispatch() {
    ActionResolvedArgument arguments[] = {
        {"light.kitchen", 0}, {"turn_on", 0}, {"{\"brightness_pct\":80}", 0}
    };
    const ActionOperationDef& operation = kHaServiceOperations[0];
    assert(operation.dispatch(arguments, "test", 0) == ACTION_COMPLETE);
    HaServiceRequest request = {};
    assert(operation_delivery.dequeue(request));
    assert(strcmp(request.payload.entity_id, arguments[0].text) == 0);
    assert(strcmp(request.payload.service, arguments[1].text) == 0);
    assert(strcmp(request.payload.data_json, arguments[2].text) == 0);

    arguments[0].text = "missing_domain";
    assert(operation.dispatch(arguments, "test", 0) == ACTION_FAILED);
    arguments[0].text = "light.kitchen";
    arguments[2].text = "[]";
    assert(operation.dispatch(arguments, "test", 0) == ACTION_FAILED);
    assert(!operation_delivery.dequeue(request));
    arguments[2].text = "{\"brightness_pct\":80}";

    for (unsigned index = 0; index < HA_SERVICE_QUEUE_CAPACITY; ++index)
        assert(operation.dispatch(arguments, "test", 0) == ACTION_COMPLETE);
    assert(operation.dispatch(arguments, "test", 0) == ACTION_FAILED);
    while (operation_delivery.dequeue(request)) {}

    operation_context.synchronous = true;
    assert(operation.dispatch(arguments, "test", 0) == ACTION_COMPLETE);
    assert(immediate_calls == 1);
    assert(strcmp(immediate_payload.entity_id, arguments[0].text) == 0);
    assert(strcmp(immediate_payload.service, arguments[1].text) == 0);
    assert(strcmp(immediate_payload.data_json, arguments[2].text) == 0);
    immediate_result = false;
    assert(operation.dispatch(arguments, "test", 0) == ACTION_FAILED);
    assert(immediate_calls == 2);
    assert(!operation_delivery.dequeue(request));
    operation_context.synchronous = false;
}

static HaServicePayload payload(const char* entity, const char* service) {
    HaServicePayload value = {};
    strncpy(value.entity_id, entity, sizeof(value.entity_id) - 1);
    strncpy(value.service, service, sizeof(value.service) - 1);
    return value;
}

static HaServiceResult terminal(uint32_t execution_id, uint8_t action_index,
                                HaServiceStatus status, int16_t http_status = HA_HTTP_STATUS_NONE) {
    HaServiceResult result = {};
    result.execution_id = execution_id;
    result.action_index = action_index;
    result.status = status;
    result.http_status = http_status;
    result.duration_ms = 25;
    return result;
}

static void test_fifo() {
    HaServiceDelivery delivery;
    assert(delivery.enqueue(payload("light.one", "turn_on"), 9, 2) == HA_SERVICE_ACCEPTED);
    assert(delivery.enqueue(payload("light.two", "turn_off")) == HA_SERVICE_ACCEPTED);
    assert(delivery.enqueue(payload("light.three", "toggle")) == HA_SERVICE_ACCEPTED);
    assert(delivery.enqueue(payload("light.four", "toggle")) == HA_SERVICE_QUEUE_FULL);

    HaServiceRequest request = {};
    assert(delivery.dequeue(request));
    assert(strcmp(request.payload.entity_id, "light.one") == 0);
    assert(request.execution_id == 9);
    assert(request.action_index == 2);
    assert(delivery.enqueue(payload("light.four", "toggle")) == HA_SERVICE_ACCEPTED);
    assert(delivery.dequeue(request) && strcmp(request.payload.entity_id, "light.two") == 0);
    assert(delivery.dequeue(request) && strcmp(request.payload.entity_id, "light.three") == 0);
    assert(delivery.dequeue(request) && strcmp(request.payload.entity_id, "light.four") == 0);
    assert(!delivery.dequeue(request));
}

static void test_registry() {
    HaServiceDelivery delivery;
    uint32_t execution_id = 0;
    assert(delivery.execution_reserve(2, 100, execution_id));
    assert(execution_id != 0);
    assert(delivery.execution_set_action(execution_id, 0, 0,
                                         payload("light.one", "turn_on")));
    assert(delivery.execution_set_action(execution_id, 1, 2,
                                         payload("light.two", "turn_off")));

    HaExecutionSnapshot snapshot = {};
    assert(delivery.execution_snapshot(execution_id, 110, snapshot) == HA_EXECUTION_FOUND);
    assert(snapshot.state == HA_EXECUTION_PENDING);
    assert(snapshot.actions[1].action_index == 2);

    HaServiceResult success = terminal(execution_id, 0, HA_STATUS_SUCCESS, 200);
    strcpy(success.entity_id, "light.one");
    strcpy(success.service, "turn_on");
    assert(delivery.execution_complete(success, 120));
    assert(delivery.execution_snapshot(execution_id, 121, snapshot) == HA_EXECUTION_FOUND);
    assert(snapshot.state == HA_EXECUTION_PENDING);

    HaServiceResult rejected = terminal(execution_id, 2, HA_STATUS_QUEUE_FULL);
    strcpy(rejected.entity_id, "light.two");
    strcpy(rejected.service, "turn_off");
    assert(delivery.execution_complete(rejected, 130));
    assert(!delivery.execution_complete(rejected, 131));
    assert(delivery.execution_snapshot(execution_id, 131, snapshot) == HA_EXECUTION_FOUND);
    assert(snapshot.state == HA_EXECUTION_COMPLETED);
    assert(snapshot.actions[0].status == HA_STATUS_SUCCESS);
    assert(snapshot.actions[1].status == HA_STATUS_QUEUE_FULL);

    assert(delivery.execution_snapshot(execution_id, 130 + HA_SERVICE_RETENTION_MS,
                                       snapshot) == HA_EXECUTION_WAS_EXPIRED);
    assert(snapshot.state == HA_EXECUTION_EXPIRED);
    assert(delivery.execution_snapshot(execution_id, 131 + HA_SERVICE_RETENTION_MS,
                                       snapshot) == HA_EXECUTION_NOT_FOUND);
}

static void test_cancelled_suffix_preserves_pending_delivery() {
    HaServiceDelivery delivery;
    uint32_t execution_id = 0;
    assert(delivery.execution_reserve(2, 100, execution_id));
    assert(delivery.execution_set_action(execution_id, 0, 1, payload("light.one", "turn_on")));
    assert(delivery.execution_set_action(execution_id, 1, 2, payload("", "")));
    assert(delivery.execution_complete(terminal(execution_id, 2, HA_STATUS_CANCELLED), 101));
    HaExecutionSnapshot snapshot = {};
    assert(delivery.execution_snapshot(execution_id, 102, snapshot) == HA_EXECUTION_FOUND);
    assert(snapshot.state == HA_EXECUTION_PENDING &&
           snapshot.actions[0].status == HA_STATUS_PENDING &&
           snapshot.actions[1].status == HA_STATUS_CANCELLED);
    HaServiceResult delivered = terminal(execution_id, 1, HA_STATUS_SUCCESS, 200);
    strcpy(delivered.entity_id, "light.one");
    strcpy(delivered.service, "turn_on");
    assert(delivery.execution_complete(delivered, 103));
    assert(delivery.execution_snapshot(execution_id, 104, snapshot) == HA_EXECUTION_FOUND);
    assert(snapshot.state == HA_EXECUTION_COMPLETED);
    JsonDocument document;
    ha_service_serialize_snapshot(snapshot, document.to<JsonObject>());
    assert(strcmp(document["actions"][1]["status"], "cancelled") == 0);
}

static void test_slot_exhaustion_and_wraparound_expiry() {
    HaServiceDelivery delivery;
    uint32_t ids[HA_SERVICE_EXECUTION_SLOTS] = {};
    for (uint8_t index = 0; index < HA_SERVICE_EXECUTION_SLOTS; index++) {
        assert(delivery.execution_reserve(1, UINT32_MAX - 20, ids[index]));
        assert(delivery.execution_set_action(ids[index], 0, 0,
                                             payload("light.test", "toggle")));
    }
    uint32_t extra = 0;
    assert(!delivery.execution_reserve(1, UINT32_MAX - 10, extra));

    HaServiceResult done = terminal(ids[0], 0, HA_STATUS_SUCCESS, 204);
    assert(delivery.execution_complete(done, UINT32_MAX - 5));
    assert(!delivery.execution_reserve(1, 3, extra));
    assert(delivery.execution_reserve(1, HA_SERVICE_RETENTION_MS - 5, extra));
}

static void test_serializer() {
    const HaServiceStatus statuses[] = {
        HA_STATUS_SUCCESS, HA_STATUS_HTTP_ERROR, HA_STATUS_TIMEOUT,
        HA_STATUS_NOT_CONFIGURED, HA_STATUS_QUEUE_FULL,
    };
    for (uint8_t index = 0; index < sizeof(statuses) / sizeof(statuses[0]); index++) {
        HaExecutionSnapshot one = {};
        one.execution_id = 42 + index;
        one.state = HA_EXECUTION_COMPLETED;
        one.action_count = 1;
        one.actions[0] = terminal(one.execution_id, index, statuses[index]);
        strcpy(one.actions[0].entity_id, "light.test");
        strcpy(one.actions[0].service, "toggle");
        if (statuses[index] == HA_STATUS_SUCCESS) one.actions[0].http_status = 200;
        if (statuses[index] == HA_STATUS_HTTP_ERROR) one.actions[0].http_status = 500;

        JsonDocument one_doc;
        ha_service_serialize_snapshot(one, one_doc.to<JsonObject>());
        assert(strcmp(one_doc["state"], "completed") == 0);
        assert(strcmp(one_doc["actions"][0]["status"],
                      ha_service_status_name(statuses[index])) == 0);
        if (statuses[index] == HA_STATUS_SUCCESS || statuses[index] == HA_STATUS_HTTP_ERROR) {
            assert(one_doc["actions"][0].containsKey("http_status"));
        } else {
            assert(!one_doc["actions"][0].containsKey("http_status"));
        }
    }

    HaExecutionSnapshot snapshot = {};
    snapshot.execution_id = 50;
    snapshot.action_count = 1;
    snapshot.state = HA_EXECUTION_PENDING;
    snapshot.actions[0].status = HA_STATUS_PENDING;
    strcpy(snapshot.actions[0].entity_id, "light.pending");
    strcpy(snapshot.actions[0].service, "toggle");
    snapshot.actions[0].http_status = HA_HTTP_STATUS_NONE;
    JsonDocument doc;
    doc.clear();
    ha_service_serialize_snapshot(snapshot, doc.to<JsonObject>());
    assert(strcmp(doc["state"], "pending") == 0);
    assert(strcmp(doc["actions"][0]["status"], "pending") == 0);

    snapshot.state = HA_EXECUTION_EXPIRED;
    snapshot.action_count = 0;
    doc.clear();
    ha_service_serialize_snapshot(snapshot, doc.to<JsonObject>());
    assert(strcmp(doc["state"], "expired") == 0);
    assert(!doc.containsKey("actions"));
}

int main() {
    static_assert(sizeof(HaServiceDelivery) < 2040,
                  "delivery state leaves no room for the firmware lock");
    test_operation_dispatch();
    test_fifo();
    test_registry();
    test_cancelled_suffix_preserves_pending_delivery();
    test_slot_exhaustion_and_wraparound_expiry();
    test_serializer();
    printf("HA delivery state: %zu bytes\n", sizeof(HaServiceDelivery));
    puts("All HA service delivery tests passed");
    return 0;
}
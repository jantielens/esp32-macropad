#include "ha_service_delivery.h"

#include <string.h>

#if HAS_MCP
static void copy_bounded(char* dest, size_t dest_size, const char* source) {
    if (!dest_size) return;
    strncpy(dest, source ? source : "", dest_size - 1);
    dest[dest_size - 1] = '\0';
}
#endif

HaServiceEnqueueResult HaServiceDelivery::enqueue(const HaServicePayload& payload,
                                                  uint32_t execution_id,
                                                  uint8_t action_index) {
    if (queue_count_ >= HA_SERVICE_QUEUE_CAPACITY) return HA_SERVICE_QUEUE_FULL;
    const uint8_t tail = (uint8_t)((queue_head_ + queue_count_) % HA_SERVICE_QUEUE_CAPACITY);
    queue_payloads_[tail] = payload;
    queue_execution_ids_[tail] = execution_id;
    queue_action_indices_[tail] = action_index;
    queue_count_++;
    return HA_SERVICE_ACCEPTED;
}

bool HaServiceDelivery::dequeue(HaServiceRequest& request) {
    if (!queue_count_) return false;
    request.payload = queue_payloads_[queue_head_];
    request.execution_id = queue_execution_ids_[queue_head_];
    request.action_index = queue_action_indices_[queue_head_];
    queue_head_ = (uint8_t)((queue_head_ + 1) % HA_SERVICE_QUEUE_CAPACITY);
    queue_count_--;
    return true;
}

const char* ha_service_status_name(HaServiceStatus status) {
    switch (status) {
        case HA_STATUS_PENDING: return "pending";
        case HA_STATUS_SUCCESS: return "success";
        case HA_STATUS_NOT_CONFIGURED: return "not_configured";
        case HA_STATUS_WIFI_DISCONNECTED: return "wifi_disconnected";
        case HA_STATUS_INVALID_REQUEST: return "invalid_request";
        case HA_STATUS_HTTP_BEGIN_FAILED: return "http_begin_failed";
        case HA_STATUS_TIMEOUT: return "timeout";
        case HA_STATUS_TRANSPORT_ERROR: return "transport_error";
        case HA_STATUS_HTTP_ERROR: return "http_error";
        case HA_STATUS_QUEUE_FULL: return "queue_full";
    }
    return "transport_error";
}

#if HAS_MCP
int HaServiceDelivery::find_execution(uint32_t execution_id) const {
    if (!execution_id) return -1;
    for (uint8_t slot = 0; slot < HA_SERVICE_EXECUTION_SLOTS; slot++) {
        if (execution_ids_[slot] == execution_id) return slot;
    }
    return -1;
}

HaServiceStatus HaServiceDelivery::get_status(uint8_t slot,
                                              uint8_t result_index) const {
    const uint8_t packed = result_statuses_[slot][result_index / 2];
    const uint8_t shift = (uint8_t)((result_index % 2) * 4);
    return (HaServiceStatus)((packed >> shift) & 0x0f);
}

void HaServiceDelivery::set_status(uint8_t slot, uint8_t result_index,
                                   HaServiceStatus status) {
    uint8_t& packed = result_statuses_[slot][result_index / 2];
    const uint8_t shift = (uint8_t)((result_index % 2) * 4);
    packed = (uint8_t)((packed & ~(0x0fU << shift)) |
                       (((uint8_t)status & 0x0fU) << shift));
}

uint8_t HaServiceDelivery::get_action_index(uint8_t slot,
                                            uint8_t result_index) const {
    return (uint8_t)((result_action_indices_[slot] >> (result_index * 2)) & 0x03);
}

void HaServiceDelivery::set_action_index(uint8_t slot, uint8_t result_index,
                                         uint8_t action_index) {
    const uint8_t shift = (uint8_t)(result_index * 2);
    result_action_indices_[slot] =
        (uint8_t)((result_action_indices_[slot] & ~(0x03U << shift)) |
                  ((action_index & 0x03U) << shift));
}

bool HaServiceDelivery::execution_is_completed(uint8_t slot) const {
    const uint8_t count = execution_action_counts_[slot];
    if (!count) return false;
    for (uint8_t index = 0; index < count; index++) {
        if (get_status(slot, index) == HA_STATUS_PENDING) return false;
    }
    return true;
}

void HaServiceDelivery::clear_execution(uint8_t slot) {
    execution_ids_[slot] = 0;
    execution_created_ms_[slot] = 0;
    execution_completed_ms_[slot] = 0;
    execution_action_counts_[slot] = 0;
    result_action_indices_[slot] = 0;
    memset(result_statuses_[slot], 0, sizeof(result_statuses_[slot]));
}

bool HaServiceDelivery::execution_reserve(uint8_t action_count, uint32_t now_ms,
                                          uint32_t& execution_id) {
    if (!action_count || action_count > MAX_BUTTON_ACTIONS) return false;

    int available = -1;
    for (uint8_t slot = 0; slot < HA_SERVICE_EXECUTION_SLOTS; slot++) {
        if (execution_ids_[slot] && execution_is_completed(slot) &&
            (uint32_t)(now_ms - execution_completed_ms_[slot]) >= HA_SERVICE_RETENTION_MS) {
            clear_execution(slot);
        }
        if (!execution_ids_[slot] && available < 0) available = slot;
    }
    if (available < 0) return false;

    uint32_t candidate = next_execution_id_;
    do {
        candidate++;
        if (!candidate) candidate++;
    } while (find_execution(candidate) >= 0);
    next_execution_id_ = candidate;

    const uint8_t slot = (uint8_t)available;
    execution_ids_[slot] = candidate;
    execution_created_ms_[slot] = now_ms;
    execution_completed_ms_[slot] = 0;
    execution_action_counts_[slot] = action_count;
    result_action_indices_[slot] = 0;
    memset(result_statuses_[slot], 0, sizeof(result_statuses_[slot]));
    for (uint8_t index = 0; index < action_count; index++) {
        result_entity_ids_[slot][index][0] = '\0';
        result_services_[slot][index][0] = '\0';
        result_duration_ms_[slot][index] = 0;
        result_http_status_[slot][index] = HA_HTTP_STATUS_NONE;
    }
    execution_id = candidate;
    return true;
}

bool HaServiceDelivery::execution_set_action(uint32_t execution_id,
                                             uint8_t result_index,
                                             uint8_t action_index,
                                             const HaServicePayload& payload) {
    const int found = find_execution(execution_id);
    if (found < 0 || result_index >= execution_action_counts_[found] ||
        action_index >= MAX_BUTTON_ACTIONS) return false;
    const uint8_t slot = (uint8_t)found;
    set_action_index(slot, result_index, action_index);
    set_status(slot, result_index, HA_STATUS_PENDING);
    copy_bounded(result_entity_ids_[slot][result_index],
                 sizeof(result_entity_ids_[slot][result_index]), payload.entity_id);
    copy_bounded(result_services_[slot][result_index],
                 sizeof(result_services_[slot][result_index]), payload.service);
    return true;
}

bool HaServiceDelivery::execution_complete(const HaServiceResult& result,
                                           uint32_t now_ms) {
    const int found = find_execution(result.execution_id);
    if (found < 0 || result.status == HA_STATUS_PENDING) return false;
    const uint8_t slot = (uint8_t)found;
    for (uint8_t index = 0; index < execution_action_counts_[slot]; index++) {
        if (get_action_index(slot, index) != result.action_index) continue;
        if (get_status(slot, index) != HA_STATUS_PENDING) return false;
        set_status(slot, index, result.status);
        result_http_status_[slot][index] = result.http_status;
        result_duration_ms_[slot][index] = result.duration_ms;
        copy_bounded(result_entity_ids_[slot][index],
                     sizeof(result_entity_ids_[slot][index]), result.entity_id);
        copy_bounded(result_services_[slot][index],
                     sizeof(result_services_[slot][index]), result.service);
        if (execution_is_completed(slot)) execution_completed_ms_[slot] = now_ms;
        return true;
    }
    return false;
}

HaExecutionLookupResult HaServiceDelivery::execution_snapshot(
    uint32_t execution_id, uint32_t now_ms, HaExecutionSnapshot& snapshot) {
    memset(&snapshot, 0, sizeof(snapshot));
    const int found = find_execution(execution_id);
    if (found < 0) return HA_EXECUTION_NOT_FOUND;
    const uint8_t slot = (uint8_t)found;
    if (execution_is_completed(slot) &&
        (uint32_t)(now_ms - execution_completed_ms_[slot]) >= HA_SERVICE_RETENTION_MS) {
        snapshot.execution_id = execution_id;
        snapshot.state = HA_EXECUTION_EXPIRED;
        clear_execution(slot);
        return HA_EXECUTION_WAS_EXPIRED;
    }

    snapshot.execution_id = execution_id;
    snapshot.created_ms = execution_created_ms_[slot];
    snapshot.completed_ms = execution_completed_ms_[slot];
    snapshot.action_count = execution_action_counts_[slot];
    snapshot.state = execution_is_completed(slot) ? HA_EXECUTION_COMPLETED
                                                   : HA_EXECUTION_PENDING;
    for (uint8_t index = 0; index < snapshot.action_count; index++) {
        HaServiceResult& result = snapshot.actions[index];
        result.execution_id = execution_id;
        result.action_index = get_action_index(slot, index);
        result.status = get_status(slot, index);
        result.http_status = result_http_status_[slot][index];
        result.duration_ms = result_duration_ms_[slot][index];
        copy_bounded(result.entity_id, sizeof(result.entity_id),
                     result_entity_ids_[slot][index]);
        copy_bounded(result.service, sizeof(result.service),
                     result_services_[slot][index]);
    }
    return HA_EXECUTION_FOUND;
}

void ha_service_serialize_snapshot(const HaExecutionSnapshot& snapshot,
                                   JsonObject result) {
    result["execution_id"] = snapshot.execution_id;
    if (snapshot.state == HA_EXECUTION_EXPIRED) {
        result["state"] = "expired";
        return;
    }
    result["state"] = snapshot.state == HA_EXECUTION_COMPLETED ? "completed" : "pending";
    JsonArray actions = result.createNestedArray("actions");
    for (uint8_t index = 0; index < snapshot.action_count; index++) {
        const HaServiceResult& action = snapshot.actions[index];
        JsonObject item = actions.createNestedObject();
        item["action_index"] = action.action_index;
        item["entity_id"] = JsonString(action.entity_id, JsonString::Copied);
        item["service"] = JsonString(action.service, JsonString::Copied);
        item["status"] = ha_service_status_name(action.status);
        if (action.http_status != HA_HTTP_STATUS_NONE) {
            item["http_status"] = action.http_status;
        }
        item["duration_ms"] = action.duration_ms;
    }
}
#endif
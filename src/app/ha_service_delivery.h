#pragma once

#include "pad_config.h"

#include <stddef.h>
#include <stdint.h>

#define HA_SERVICE_QUEUE_CAPACITY 3
#define HA_SERVICE_EXECUTION_SLOTS 4
#define HA_SERVICE_RETENTION_MS 60000U

enum HaServiceEnqueueResult : uint8_t {
    HA_SERVICE_ACCEPTED = 0,
    HA_SERVICE_QUEUE_FULL,
};

enum HaServiceStatus : uint8_t {
    HA_STATUS_PENDING = 0,
    HA_STATUS_SUCCESS,
    HA_STATUS_NOT_CONFIGURED,
    HA_STATUS_WIFI_DISCONNECTED,
    HA_STATUS_INVALID_REQUEST,
    HA_STATUS_HTTP_BEGIN_FAILED,
    HA_STATUS_TIMEOUT,
    HA_STATUS_TRANSPORT_ERROR,
    HA_STATUS_HTTP_ERROR,
    HA_STATUS_QUEUE_FULL,
};

static constexpr int16_t HA_HTTP_STATUS_NONE = INT16_MIN;

struct HaServiceRequest {
    HaServicePayload payload;
    uint32_t execution_id;
    uint8_t action_index;
};

struct HaServiceResult {
    uint32_t execution_id;
    uint8_t action_index;
    HaServiceStatus status;
    int16_t http_status;
    uint32_t duration_ms;
    char entity_id[sizeof(HaServicePayload::entity_id)];
    char service[sizeof(HaServicePayload::service)];
};

#if HAS_MCP
enum HaExecutionState : uint8_t {
    HA_EXECUTION_PENDING = 0,
    HA_EXECUTION_COMPLETED,
    HA_EXECUTION_EXPIRED,
};

enum HaExecutionLookupResult : uint8_t {
    HA_EXECUTION_FOUND = 0,
    HA_EXECUTION_NOT_FOUND,
    HA_EXECUTION_WAS_EXPIRED,
};

struct HaExecutionSnapshot {
    uint32_t execution_id;
    uint32_t created_ms;
    uint32_t completed_ms;
    uint8_t action_count;
    HaExecutionState state;
    HaServiceResult actions[MAX_BUTTON_ACTIONS];
};
#endif

class HaServiceDelivery {
public:
    HaServiceEnqueueResult enqueue(const HaServicePayload& payload,
                                   uint32_t execution_id = 0,
                                   uint8_t action_index = 0);
    bool dequeue(HaServiceRequest& request);

#if HAS_MCP
    bool execution_reserve(uint8_t action_count, uint32_t now_ms,
                           uint32_t& execution_id);
    bool execution_set_action(uint32_t execution_id, uint8_t result_index,
                              uint8_t action_index,
                              const HaServicePayload& payload);
    bool execution_complete(const HaServiceResult& result, uint32_t now_ms);
    HaExecutionLookupResult execution_snapshot(uint32_t execution_id,
                                                uint32_t now_ms,
                                                HaExecutionSnapshot& snapshot);
#endif

private:
    HaServicePayload queue_payloads_[HA_SERVICE_QUEUE_CAPACITY] = {};
    uint32_t queue_execution_ids_[HA_SERVICE_QUEUE_CAPACITY] = {};
    uint8_t queue_action_indices_[HA_SERVICE_QUEUE_CAPACITY] = {};
    uint8_t queue_head_ = 0;
    uint8_t queue_count_ = 0;

#if HAS_MCP
    uint32_t execution_ids_[HA_SERVICE_EXECUTION_SLOTS] = {};
    uint32_t execution_created_ms_[HA_SERVICE_EXECUTION_SLOTS] = {};
    uint32_t execution_completed_ms_[HA_SERVICE_EXECUTION_SLOTS] = {};
    uint32_t next_execution_id_ = 0;
    char result_entity_ids_[HA_SERVICE_EXECUTION_SLOTS][MAX_BUTTON_ACTIONS]
                           [sizeof(HaServicePayload::entity_id)] = {};
    char result_services_[HA_SERVICE_EXECUTION_SLOTS][MAX_BUTTON_ACTIONS]
                         [sizeof(HaServicePayload::service)] = {};
    uint32_t result_duration_ms_[HA_SERVICE_EXECUTION_SLOTS]
                                [MAX_BUTTON_ACTIONS] = {};
    int16_t result_http_status_[HA_SERVICE_EXECUTION_SLOTS]
                               [MAX_BUTTON_ACTIONS] = {};
    uint8_t result_action_indices_[HA_SERVICE_EXECUTION_SLOTS] = {};
    uint8_t result_statuses_[HA_SERVICE_EXECUTION_SLOTS][2] = {};
    uint8_t execution_action_counts_[HA_SERVICE_EXECUTION_SLOTS] = {};

    int find_execution(uint32_t execution_id) const;
    bool execution_is_completed(uint8_t slot) const;
    HaServiceStatus get_status(uint8_t slot, uint8_t result_index) const;
    void set_status(uint8_t slot, uint8_t result_index, HaServiceStatus status);
    uint8_t get_action_index(uint8_t slot, uint8_t result_index) const;
    void set_action_index(uint8_t slot, uint8_t result_index, uint8_t action_index);
    void clear_execution(uint8_t slot);
#endif
};

static_assert(HA_SERVICE_QUEUE_CAPACITY >= MAX_BUTTON_ACTIONS,
              "HA queue must accept one complete action list");

const char* ha_service_status_name(HaServiceStatus status);

#if HAS_MCP
#include <ArduinoJson.h>
void ha_service_serialize_snapshot(const HaExecutionSnapshot& snapshot,
                                   JsonObject result);
#endif
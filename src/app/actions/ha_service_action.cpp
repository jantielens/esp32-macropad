#include "action_registry.h"
#include "action_dispatch.h"
#include "action_validate.h"
#if HAS_MCP
#include "action_run_context.h"
#endif
#include "log_manager.h"
#if defined(ARDUINO)
#include "ha_service.h"
#endif
#include <ctype.h>

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kHaServiceActionTag = "Action";
const char* validate_ha_entity(const char* entity);
const char* validate_ha_service_name(const char* service);
const char* validate_ha_service_data(const char* data);
ActionResult dispatch_ha_service(const HaServicePayload& payload, const char* label, uint32_t) {
    const char* error = validate_ha_entity(payload.entity_id);
    if (!error) error = validate_ha_service_name(payload.service);
    if (!error) error = validate_ha_service_data(payload.data_json);
    if (error) {
        LOGW(kHaServiceActionTag, "%s ha_service: %s", label, error);
        return ACTION_FAILED;
    }
#if defined(ARDUINO)
    const auto& service = payload;
#if HAS_MCP
    ActionResult tracked_result;
    if (action_run_dispatch_ha(service, &tracked_result)) return tracked_result;
#endif
    if (action_dispatch_context().synchronous)
        return ha_service_execute_immediate(service) ? ACTION_COMPLETE : ACTION_FAILED;
    if (service.entity_id[0] && service.service[0]) { LOGI(kHaServiceActionTag, "%s ha_service: %s.%s", label, service.entity_id, service.service); if (ha_service_enqueue(service) == HA_SERVICE_QUEUE_FULL) { LOGW(kHaServiceActionTag, "%s ha_service queue full: entity='%s' service='%s'", label, service.entity_id, service.service); return ACTION_FAILED; } }
    else { LOGW(kHaServiceActionTag, "%s ha_service: missing entity_id/service", label); return ACTION_FAILED; }
#else
    (void)label;
#endif
    return ACTION_COMPLETE;
}
bool ha_service_available() { return true; }
const char* validate_ha_entity(const char* entity) {
    const char* separator = strchr(entity, '.');
    if (!entity[0] || strlen(entity) >= sizeof(((HaServicePayload*)nullptr)->entity_id) || !separator || separator == entity || !separator[1] || strchr(separator + 1, '.')) return "ha_service entity_id must have nonempty domain and object portions";
    for (const char* cursor = entity; *cursor; ++cursor) if (isspace((unsigned char)*cursor)) return "ha_service entity_id must not contain whitespace";
    return nullptr;
}
const char* validate_ha_service_name(const char* service) {
    if (!service[0] || strlen(service) >= sizeof(((HaServicePayload*)nullptr)->service) || strchr(service, '.')) return "ha_service service must be a bare string";
    for (const char* cursor = service; *cursor; ++cursor) if (!islower((unsigned char)*cursor) && !isdigit((unsigned char)*cursor) && *cursor != '_') return "ha_service service must contain only lowercase letters, digits, and '_'";
    return nullptr;
}
const char* validate_ha_service_data(const char* data) {
    if (!data[0]) return nullptr;
    if (strlen(data) >= sizeof(((HaServicePayload*)nullptr)->data_json)) return "ha_service data_json too long";
    return action_json_object_valid(data) ? nullptr : "ha_service data_json must contain a JSON object";
}
const ActionArgumentDef kHaServiceCallArguments[] = {
    {"entity_id", "Entity ID", ACTION_ARGUMENT_STRING, 0, 0,
     sizeof(((HaServicePayload*)nullptr)->entity_id) - 1, true, "media_player.living_room", "text",
     nullptr, 0, false, validate_ha_entity, nullptr, 0, nullptr, 0, false, nullptr,
     "Home Assistant entity ID with domain and object portions, such as media_player.living_room. The entity domain determines the service domain."},
    {"service", "Service", ACTION_ARGUMENT_STRING, 0, 0,
     sizeof(((HaServicePayload*)nullptr)->service) - 1, true, "media_play_pause", "text",
     nullptr, 0, false, validate_ha_service_name, nullptr, 0, nullptr, 0, false, nullptr,
     "Bare service name, such as media_play_pause, NOT media_player.media_play_pause. The domain is inferred from entity_id. Only lowercase letters, digits, and underscores are allowed."},
    {"data_json", "Service data (JSON object)", ACTION_ARGUMENT_STRING, 0, 0,
     sizeof(((HaServicePayload*)nullptr)->data_json) - 1, true, "{}", "json_object",
     nullptr, 0, true, validate_ha_service_data, nullptr, 0, nullptr, 0, false, nullptr,
     "Service data encoded as a JSON object string, such as {} or {\"volume_level\":0.5}. Empty means no extra service data."},
};
ActionResult dispatch_ha_service_call(const ActionResolvedArgument* arguments, const char* label,
                                      uint32_t token) {
    HaServicePayload payload = {};
    strlcpy(payload.entity_id, arguments[0].text, sizeof(payload.entity_id));
    strlcpy(payload.service, arguments[1].text, sizeof(payload.service));
    strlcpy(payload.data_json, arguments[2].text, sizeof(payload.data_json));
    return dispatch_ha_service(payload, label, token);
}
const ActionOperationDef kHaServiceOperations[] = {
    {"ha.service.call", "Call Home Assistant service", kHaServiceCallArguments, 3,
     dispatch_ha_service_call},
};
void describe_ha_service(JsonObject& action) { action["group"] = "Connectivity"; action["label"] = "Call Home Assistant service"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kHaServiceActionType, ACTION_TYPE_HA_SERVICE, describe_ha_service, ha_service_available, ACTION_EXECUTION_SYNC, false, false,
    kHaServiceOperations, sizeof(kHaServiceOperations) / sizeof(kHaServiceOperations[0]));
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON
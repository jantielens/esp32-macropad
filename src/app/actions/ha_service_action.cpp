#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO)
#include "ha_service.h"
#endif
#include <ctype.h>

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kHaServiceActionTag = "Action";
void parse_ha_service(const JsonObject& action, ButtonAction& act) { strlcpy(act.payload.ha_service.entity_id, action["entity_id"] | "", sizeof(act.payload.ha_service.entity_id)); strlcpy(act.payload.ha_service.service, action["service"] | "", sizeof(act.payload.ha_service.service)); strlcpy(act.payload.ha_service.data_json, action["data_json"] | "", sizeof(act.payload.ha_service.data_json)); }
void serialize_ha_service(const ButtonAction& act, JsonObject action) { if (act.payload.ha_service.entity_id[0]) action["entity_id"] = act.payload.ha_service.entity_id; if (act.payload.ha_service.service[0]) action["service"] = act.payload.ha_service.service; if (act.payload.ha_service.data_json[0]) action["data_json"] = act.payload.ha_service.data_json; }
ActionResult dispatch_ha_service(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO)
    const auto& service = act.payload.ha_service;
    if (service.entity_id[0] && service.service[0]) { LOGI(kHaServiceActionTag, "%s ha_service: %s.%s", label, service.entity_id, service.service); if (ha_service_enqueue(service) == HA_SERVICE_QUEUE_FULL) LOGW(kHaServiceActionTag, "%s ha_service queue full: entity='%s' service='%s'", label, service.entity_id, service.service); }
    else LOGW(kHaServiceActionTag, "%s ha_service: missing entity_id/service", label);
#else
    (void)act; (void)label;
#endif
    return ACTION_COMPLETE;
}
bool ha_service_available() { return true; }
const char* validate_ha_service(const JsonObjectConst action) {
    if (!action.containsKey("entity_id") || !action["entity_id"].is<const char*>()) return "ha_service entity_id must be a string";
    const char* entity = action["entity_id"].as<const char*>(); const char* separator = strchr(entity, '.');
    if (!entity[0] || strlen(entity) >= sizeof(((HaServicePayload*)nullptr)->entity_id) || !separator || separator == entity || !separator[1] || strchr(separator + 1, '.')) return "ha_service entity_id must have nonempty domain and object portions";
    for (const char* cursor = entity; *cursor; ++cursor) if (isspace((unsigned char)*cursor)) return "ha_service entity_id must not contain whitespace";
    if (!action.containsKey("service") || !action["service"].is<const char*>()) return "ha_service service must be a string";
    const char* service = action["service"].as<const char*>();
    if (!service[0] || strlen(service) >= sizeof(((HaServicePayload*)nullptr)->service) || strchr(service, '.')) return "ha_service service must be a bare string";
    for (const char* cursor = service; *cursor; ++cursor) if (!islower((unsigned char)*cursor) && !isdigit((unsigned char)*cursor) && *cursor != '_') return "ha_service service must contain only lowercase letters, digits, and '_'";
    if (!action.containsKey("data_json")) return nullptr;
    if (!action["data_json"].is<const char*>()) return "ha_service data_json must be a string";
    const char* data = action["data_json"].as<const char*>(); if (!data[0]) return nullptr;
    if (strlen(data) >= sizeof(((HaServicePayload*)nullptr)->data_json)) return "ha_service data_json too long";
    JsonDocument document; return deserializeJson(document, data) || !document.is<JsonObjectConst>() ? "ha_service data_json must contain a JSON object" : nullptr;
}
void describe_ha_service(JsonObject& action) { action["group"] = "Connectivity"; action["label"] = "Call Home Assistant service"; JsonArray fields = action.createNestedArray("fields"); JsonObject entity = fields.createNestedObject(); entity["name"] = "entity_id"; entity["description"] = "required domain-qualified entity"; JsonObject service = fields.createNestedObject(); service["name"] = "service"; service["description"] = "required bare service name"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kHaServiceActionType, ACTION_TYPE_HA_SERVICE, parse_ha_service, serialize_ha_service, dispatch_ha_service, nullptr, describe_ha_service, ha_service_available, validate_ha_service);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON
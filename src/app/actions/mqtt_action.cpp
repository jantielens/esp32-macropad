#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_MQTT
#include "mqtt_manager.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kMqttActionTag = "Action";
void parse_mqtt(const JsonObject& action, ButtonAction& act) { strlcpy(act.payload.mqtt.mqtt_topic, action["topic"] | "", sizeof(act.payload.mqtt.mqtt_topic)); strlcpy(act.payload.mqtt.mqtt_payload, action["payload"] | "", sizeof(act.payload.mqtt.mqtt_payload)); }
void serialize_mqtt(const ButtonAction& act, JsonObject action) { if (act.payload.mqtt.mqtt_topic[0]) action["topic"] = act.payload.mqtt.mqtt_topic; if (act.payload.mqtt.mqtt_payload[0]) action["payload"] = act.payload.mqtt.mqtt_payload; }
ActionResult dispatch_mqtt(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_MQTT
    const auto& mqtt = act.payload.mqtt;
    if (mqtt.mqtt_topic[0]) { bool ok = mqtt_manager.publish(mqtt.mqtt_topic, mqtt.mqtt_payload, false); LOGI(kMqttActionTag, "%s mqtt: topic='%s' payload='%s' %s", label, mqtt.mqtt_topic, mqtt.mqtt_payload, ok ? "ok" : "FAIL"); }
    else LOGW(kMqttActionTag, "%s mqtt: empty topic", label);
#else
    (void)act;
    LOGW(kMqttActionTag, "%s mqtt: not compiled", label);
#endif
    return ACTION_COMPLETE;
}
bool mqtt_available() { return HAS_MQTT; }
const char* validate_mqtt(const JsonObjectConst action) { if (action.containsKey("topic") && !action["topic"].is<const char*>()) return "mqtt topic must be a string"; return action.containsKey("payload") && !action["payload"].is<const char*>() ? "mqtt payload must be a string" : nullptr; }
bool visit_mqtt_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) { return (!act.payload.mqtt.mqtt_topic[0] || visitor(act.payload.mqtt.mqtt_topic, sizeof(act.payload.mqtt.mqtt_topic), false, context)) && (!act.payload.mqtt.mqtt_payload[0] || visitor(act.payload.mqtt.mqtt_payload, sizeof(act.payload.mqtt.mqtt_payload), false, context)); }
void describe_mqtt(JsonObject& action) { action["group"] = "Connectivity"; action["label"] = "Publish MQTT message"; JsonArray fields = action.createNestedArray("fields"); JsonObject topic = fields.createNestedObject(); topic["name"] = "topic"; topic["description"] = "MQTT topic to publish to"; JsonObject payload = fields.createNestedObject(); payload["name"] = "payload"; payload["description"] = "MQTT payload"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kMqttActionType, ACTION_TYPE_MQTT, parse_mqtt, serialize_mqtt, dispatch_mqtt, nullptr, describe_mqtt, mqtt_available, validate_mqtt, visit_mqtt_fields);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON
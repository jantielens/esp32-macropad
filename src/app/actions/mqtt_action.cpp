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
ActionResult publish_mqtt(const char* topic, const char* payload, const char* label) {
#if defined(ARDUINO) && HAS_MQTT
    if (topic[0]) {
        const bool ok = mqtt_manager.publish(topic, payload, false);
        if (ok) LOGT(kMqttActionTag, "%s mqtt: topic='%s' bytes=%u", label, topic, unsigned(strlen(payload)));
        else LOGW(kMqttActionTag, "%s mqtt publish failed: topic='%s'", label, topic);
        return ok ? ACTION_COMPLETE : ACTION_FAILED;
    }
    LOGW(kMqttActionTag, "%s mqtt: empty topic", label);
#else
    (void)topic;
    (void)payload;
    LOGW(kMqttActionTag, "%s mqtt: not compiled", label);
#endif
    return ACTION_FAILED;
}
ActionResult dispatch_mqtt(const ButtonAction& act, const char* label, uint32_t) {
    publish_mqtt(act.payload.mqtt.mqtt_topic, act.payload.mqtt.mqtt_payload, label);
    return ACTION_COMPLETE;
}
const ActionArgumentDef kMqttPublishArguments[] = {
    {"topic", "Topic", ACTION_ARGUMENT_STRING, 0, 0, CONFIG_MQTT_TOPIC_MAX_LEN - 1,
     true, "[var:topic]", "text"},
    {"payload", "Payload", ACTION_ARGUMENT_STRING, 0, 0, CONFIG_MQTT_PAYLOAD_MAX_LEN - 1,
     true, "[var:payload]", "text", nullptr, 0, true},
};
ActionResult dispatch_mqtt_publish(const ActionResolvedArgument* arguments, const char* label,
                                   uint32_t) {
    return publish_mqtt(arguments[0].text, arguments[1].text, label);
}
const ActionOperationDef kMqttOperations[] = {
    {"mqtt.publish", "Publish MQTT message", kMqttPublishArguments, 2, dispatch_mqtt_publish},
};
bool mqtt_available() { return HAS_MQTT; }
const char* validate_mqtt(const JsonObjectConst action) {
    if (action.containsKey("topic")) {
        if (!action["topic"].is<const char*>()) return "mqtt topic must be a string";
        if (strlen(action["topic"].as<const char*>()) >= sizeof(((MqttPayload*)nullptr)->mqtt_topic))
            return "mqtt topic too long";
    }
    if (action.containsKey("payload")) {
        if (!action["payload"].is<const char*>()) return "mqtt payload must be a string";
        if (strlen(action["payload"].as<const char*>()) >= sizeof(((MqttPayload*)nullptr)->mqtt_payload))
            return "mqtt payload too long";
    }
    return nullptr;
}
bool visit_mqtt_fields(ButtonAction& act, ActionFieldVisitor visitor, void* context) {
    return action_visit_field(visitor, context, "topic", act.payload.mqtt.mqtt_topic, ACTION_FIELD_BINDING | ACTION_FIELD_EVENT) &&
        action_visit_field(visitor, context, "payload", act.payload.mqtt.mqtt_payload, ACTION_FIELD_BINDING | ACTION_FIELD_EVENT);
}
void describe_mqtt(JsonObject& action) {
    action["group"] = "Connectivity";
    action["label"] = "Publish MQTT message";
    JsonArray fields = action.createNestedArray("fields");
    JsonObject topic = fields.createNestedObject(); topic["name"] = "topic"; topic["description"] = "MQTT topic to publish to";
    JsonObject payload = fields.createNestedObject(); payload["name"] = "payload"; payload["description"] = "MQTT payload";
    JsonArray editor_fields = action.createNestedArray("editor_fields");
    JsonObject editor_topic = editor_fields.createNestedObject(); editor_topic["name"] = "topic"; editor_topic["label"] = "Topic"; editor_topic["type"] = "text"; editor_topic["bindable"] = true; editor_topic["max"] = CONFIG_MQTT_TOPIC_MAX_LEN - 1;
    JsonObject editor_payload = editor_fields.createNestedObject(); editor_payload["name"] = "payload"; editor_payload["label"] = "Payload"; editor_payload["type"] = "text"; editor_payload["bindable"] = true; editor_payload["max"] = CONFIG_MQTT_PAYLOAD_MAX_LEN - 1;
}
DEFINE_AND_REGISTER_ACTION_TYPE(kMqttActionType, ACTION_TYPE_MQTT, parse_mqtt, serialize_mqtt, dispatch_mqtt, visit_mqtt_fields, describe_mqtt, mqtt_available, validate_mqtt, ACTION_EXECUTION_SYNC, true, true, false, nullptr,
    kMqttOperations, sizeof(kMqttOperations) / sizeof(kMqttOperations[0]));
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON
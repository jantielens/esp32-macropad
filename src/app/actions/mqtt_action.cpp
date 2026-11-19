#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_MQTT
#include "mqtt_manager.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kMqttActionTag = "Action";
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
void describe_mqtt(JsonObject& action) {
    action["group"] = "Connectivity";
    action["label"] = "Publish MQTT message";
}
DEFINE_AND_REGISTER_ACTION_TYPE(kMqttActionType, ACTION_TYPE_MQTT, describe_mqtt, mqtt_available, ACTION_EXECUTION_SYNC, true, false,
    kMqttOperations, sizeof(kMqttOperations) / sizeof(kMqttOperations[0]));
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON
#include "camera_motion.h"

#if HAS_CAMERA

#include "ha_discovery.h"
#include "sensor_manager.h"
#if HAS_MQTT
#include "mqtt_manager.h"
#endif

namespace {

#if HAS_MQTT
constexpr const char* kCameraPresenceStateTopicSuffix = "camera_presence/state";
constexpr const char* kCameraPresenceDiscoveryTopicFormat =
    "homeassistant/binary_sensor/%s/camera_presence/config";
bool s_discovery_published = false;

void camera_presence_publish_ha(MqttManager& mqtt);

bool camera_presence_remove_discovery() {
    if (!mqtt_manager.connected()) return false;
    char topic[160] = {};
    if (snprintf(topic, sizeof(topic), kCameraPresenceDiscoveryTopicFormat,
                 mqtt_manager.sanitizedName()) >= static_cast<int>(sizeof(topic))) {
        return false;
    }
    return mqtt_manager.publishImmediate(topic, "", true);
}
#endif

void camera_presence_loop() {
#if HAS_MQTT
    bool presence = false;
    const CameraMotionStatus status = camera_motion_get_status();
    if (!status.enabled) {
        if (s_discovery_published && camera_presence_remove_discovery()) {
            s_discovery_published = false;
        }
        camera_motion_take_presence_change(&presence);
        return;
    }
    if (!s_discovery_published) {
        camera_presence_publish_ha(mqtt_manager);
        if (sensor_manager_publish_binary_state(kCameraPresenceStateTopicSuffix, status.presence, true)) {
            s_discovery_published = true;
        }
    }
    if (camera_motion_take_presence_change(&presence)) {
        sensor_manager_publish_binary_state(kCameraPresenceStateTopicSuffix, presence, true);
    }
#endif
}

void camera_presence_append_api(JsonObject& doc) {
    const CameraMotionStatus status = camera_motion_get_status();
    sensor_manager_set_bool(doc, "camera_presence", status.presence, status.enabled);
    if (status.enabled) {
        doc["camera_last_motion"] = status.has_last_motion ? status.last_motion_epoch : 0;
    } else {
        doc["camera_last_motion"] = nullptr;
    }
}

#if HAS_MQTT
void camera_presence_publish_ha(MqttManager& mqtt) {
    if (!camera_motion_is_enabled()) return;
    ha_discovery_publish_binary_sensor_config_with_topic_suffix(
        mqtt, "camera_presence", "Camera Presence", kCameraPresenceStateTopicSuffix,
        "presence", nullptr);
}
#endif

} // namespace

void register_camera_presence_sensor(SensorRegistry& registry) {
    SensorCallbacks callbacks = {};
    callbacks.name = "Camera Presence";
    callbacks.loop = camera_presence_loop;
    callbacks.append_api = camera_presence_append_api;
#if HAS_MQTT
    callbacks.publish_ha = camera_presence_publish_ha;
#endif
    registry.add(callbacks);
}

#endif // HAS_CAMERA
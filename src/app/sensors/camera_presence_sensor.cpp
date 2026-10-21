#include "camera_motion.h"

#if HAS_CAMERA

#include "camera_motion_actions.h"
#include "ha_discovery.h"
#include "sensor_manager.h"
#if HAS_DISPLAY
#include "screen_saver_manager.h"
#endif
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

bool camera_presence_loop() {
    bool presence = false;
    const bool presence_changed = camera_motion_take_presence_change(&presence);
#if HAS_DISPLAY
    if (camera_motion_take_confirmed_motion() && camera_motion_get_settings().keep_display_awake) {
        screen_saver_manager_notify_activity(true);
    }
#endif
#if HAS_MQTT
    const CameraMotionStatus status = camera_motion_get_status();
    if (!status.enabled) {
        if (s_discovery_published && camera_presence_remove_discovery()) {
            s_discovery_published = false;
        }
    } else if (!s_discovery_published) {
        camera_presence_publish_ha(mqtt_manager);
        if (sensor_manager_publish_binary_state(kCameraPresenceStateTopicSuffix, status.presence, true)) {
            s_discovery_published = true;
        }
    }
    if (presence_changed && status.enabled) {
        sensor_manager_publish_binary_state(kCameraPresenceStateTopicSuffix, presence, true);
    }
#endif
    if (presence_changed) {
#if CAMERA_MOTION_ACTIONS_ENABLED
        if (presence) camera_motion_actions_on_presence_started();
#endif
    }
    const CameraMotionStatus snapshot = camera_motion_get_status();
    static bool previous_enabled = false;
    static uint32_t previous_motion = 0;
    const uint32_t motion = snapshot.has_last_motion ? snapshot.last_motion_epoch : 0;
    const bool changed = presence_changed || previous_enabled != snapshot.enabled || previous_motion != motion;
    previous_enabled = snapshot.enabled;
    previous_motion = motion;
    return changed;
}

void camera_presence_sample(JsonObject& doc) {
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
    static const SensorField fields[] = {
        {"camera_presence", "Camera presence detected: 1 or 0.", "", "1"},
        {"camera_last_motion", "Last camera motion as Unix seconds; 0 if none.", "s", "0"},
    };
    SensorCallbacks callbacks = {};
    callbacks.name = "Camera Presence";
    callbacks.loop = camera_presence_loop;
    callbacks.sample = camera_presence_sample;
    callbacks.event_driven = true;
    callbacks.include_in_mqtt = false;
    callbacks.fields = fields;
    callbacks.field_count = sizeof(fields) / sizeof(fields[0]);
#if HAS_MQTT
    callbacks.publish_ha = camera_presence_publish_ha;
#endif
    registry.add(callbacks);
}

#endif // HAS_CAMERA
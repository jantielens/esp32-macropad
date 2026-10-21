#include "mqtt_notify.h"
#include "board_config.h"

#if HAS_DISPLAY && HAS_MQTT
#include "mqtt_manager.h"
#include "device_dialog.h"
#include "log_manager.h"
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <string.h>

namespace {
char command_topic[128] = {};
char state_topic[128] = {};
portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
bool pending = false;
NotifyPayload content = {};
}

void mqtt_notify_init() {
    const char* base = mqtt_manager.baseTopic();
    snprintf(command_topic, sizeof(command_topic), "%s/notify/set", base);
    snprintf(state_topic, sizeof(state_topic), "%s/notify/state", base);
}

void mqtt_notify_on_connected() {
    mqtt_manager.setSubscriptionOwner(command_topic, MqttSubscriptionOwner::Notify, true);
    mqtt_manager.publish(state_topic, "", true);
}

void mqtt_notify_on_message(const char* topic, const uint8_t* payload, unsigned int length) {
    if (strcmp(topic, command_topic)) return;
    NotifyPayload candidate{};
    candidate.duration_ms = 3000;
    if (length && payload[0] == '{') {
        JsonDocument doc;
        const auto error = deserializeJson(doc, payload, length);
        if (error || !doc["text"].is<const char*>() ||
            strlen(doc["text"].as<const char*>()) >= sizeof(candidate.message) ||
            (doc.containsKey("duration_ms") && !doc["duration_ms"].is<uint32_t>())) {
            LOGW("MqttNotify", "Invalid notification JSON or message capacity");
            return;
        }
        strlcpy(candidate.message, doc["text"], sizeof(candidate.message));
        candidate.duration_ms = doc["duration_ms"] | 3000U;
    } else {
        if (length >= sizeof(candidate.message)) {
            LOGW("MqttNotify", "Message exceeds 191 UTF-8 bytes");
            return;
        }
        if (length) memcpy(candidate.message, payload, length);
    }
    if (candidate.duration_ms && (candidate.duration_ms < 1000 || candidate.duration_ms > 300000)) {
        LOGW("MqttNotify", "Duration must be 0 or 1000-300000 milliseconds");
        return;
    }
    portENTER_CRITICAL(&mutex);
    content = candidate;
    pending = true;
    portEXIT_CRITICAL(&mutex);
}

void mqtt_notify_loop() {
    NotifyPayload candidate;
    portENTER_CRITICAL(&mutex);
    const bool ready = pending;
    if (ready) {
        candidate = content;
        pending = false;
    }
    portEXIT_CRITICAL(&mutex);
    if (!ready) return;
    if (candidate.message[0]) {
        if (!device_dialog_notify(candidate.message, candidate.duration_ms)) {
            LOGW("MqttNotify", "Notification could not be displayed");
            return;
        }
    } else {
        device_dialog_dismiss_notification();
    }
    mqtt_manager.publish(state_topic, candidate.message, true);
}
#endif

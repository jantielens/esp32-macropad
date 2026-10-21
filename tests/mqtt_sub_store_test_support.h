#pragma once

#include "board_config.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#undef HAS_DISPLAY
#ifndef TEST_HAS_DISPLAY
#define TEST_HAS_DISPLAY 1
#endif
#define HAS_DISPLAY TEST_HAS_DISPLAY
#undef HAS_MQTT
#define HAS_MQTT 1
#undef HAS_CAMERA
#define HAS_CAMERA 0

#define CONFIG_MQTT_TOPIC_MAX_LEN 128
#define CONFIG_MANAGER_H
#define PAD_CONFIG_H
#define MQTT_MANAGER_H

#include "log_manager.h"

#include "mqtt_subscription_registry.h"

struct MqttSubStoreTestOwnerCall {
    char topic[CONFIG_MQTT_TOPIC_MAX_LEN];
    MqttSubscriptionOwner owner;
    bool active;
};

void mqtt_sub_store_test_record_owner(const char* topic,
                                      MqttSubscriptionOwner owner,
                                      bool active);

struct DeviceConfig {
    char screen_saver_wake_binding[1];
};

class MqttManager {
public:
    const DeviceConfig* config() const { return nullptr; }
    bool enabled() const { return true; }
    bool connectionAvailableSnapshot() const { return true; }
    MqttSubscriptionState subscriptionState(const char*, MqttSubscriptionOwner) {
        return MqttSubscriptionState::Accepted;
    }
    MqttSubscriptionCleanup subscriptionCleanup(const char*) {
        return MqttSubscriptionCleanup::Complete;
    }

    bool setSubscriptionOwner(const char* topic,
                             MqttSubscriptionOwner owner,
                             bool active) {
        mqtt_sub_store_test_record_owner(topic, owner, active);
        return true;
    }
};

extern MqttManager mqtt_manager;

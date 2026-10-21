#pragma once

#include "pad_config.h"

#include <stdint.h>
#include <string.h>

enum class MqttSubscriptionOwner : uint8_t {
    Legacy,
    SavedBindings,
    Preview,
    Inspection,
    Screen,
    Audio,
    Camera,
    Notify,
    Triggers,
    EpaperFrame,
    Count,
};

enum class MqttSubscriptionState : uint8_t {
    Missing,
    Pending,
    Accepted,
    Failed,
};

enum class MqttSubscriptionCleanup : uint8_t {
    Complete,
    Pending,
    Failed,
};

enum class MqttSubscriptionOperationType : uint8_t {
    Subscribe,
    Unsubscribe,
};

struct MqttSubscriptionOperation {
    MqttSubscriptionOperationType type;
    char topic[CONFIG_MQTT_TOPIC_MAX_LEN];
};

class MqttSubscriptionRegistry {
public:
    static constexpr uint8_t kCapacity = 128;

    bool setOwner(const char* topic, MqttSubscriptionOwner owner, bool active) {
        if (!validTopic(topic)) return false;

        Entry* entry = find(topic);
        const uint16_t bit = ownerBit(owner);
        if (!active) {
            if (!entry) return true;
            entry->owners &= static_cast<uint16_t>(~bit);
            return true;
        }

        if (!entry) entry = allocate(topic);
        if (!entry) return false;
        if ((entry->owners & bit) == 0) {
            entry->owners |= bit;
            entry->subscribe_attempted = false;
            entry->subscribe_failed = false;
        }
        entry->cleanup_attempts = 0;
        entry->cleanup_failed = false;
        return true;
    }

    MqttSubscriptionState state(const char* topic, MqttSubscriptionOwner owner) const {
        const Entry* entry = find(topic);
        if (!entry || (entry->owners & ownerBit(owner)) == 0) return MqttSubscriptionState::Missing;
        if (entry->broker_subscribed) return MqttSubscriptionState::Accepted;
        if (entry->subscribe_attempted && entry->subscribe_failed) return MqttSubscriptionState::Failed;
        return MqttSubscriptionState::Pending;
    }

    MqttSubscriptionCleanup cleanup(const char* topic) const {
        const Entry* entry = find(topic);
        if (!entry || entry->owners != 0) return MqttSubscriptionCleanup::Complete;
        if (entry->cleanup_failed) return MqttSubscriptionCleanup::Failed;
        if (entry->broker_subscribed || entry->in_flight) return MqttSubscriptionCleanup::Pending;
        return MqttSubscriptionCleanup::Complete;
    }

    bool nextOperation(uint32_t now_ms, MqttSubscriptionOperation* operation) {
        if (!operation) return false;
        for (Entry& entry : _entries) {
            if (!entry.used || entry.in_flight) continue;
            if (entry.owners != 0 && !entry.broker_subscribed && !entry.subscribe_attempted) {
                entry.in_flight = true;
                entry.operation = MqttSubscriptionOperationType::Subscribe;
                operation->type = entry.operation;
                copyTopic(operation->topic, entry.topic);
                return true;
            }
            if (entry.owners == 0 && entry.broker_subscribed && entry.cleanup_attempts < kMaxCleanupAttempts &&
                (entry.cleanup_attempts == 0 || now_ms - entry.last_cleanup_ms >= kCleanupRetryMs)) {
                entry.in_flight = true;
                entry.operation = MqttSubscriptionOperationType::Unsubscribe;
                operation->type = entry.operation;
                copyTopic(operation->topic, entry.topic);
                return true;
            }
            if (entry.owners == 0 && !entry.broker_subscribed) clear(entry);
        }
        return false;
    }

    void complete(const MqttSubscriptionOperation& operation, bool success, uint32_t now_ms) {
        Entry* entry = find(operation.topic);
        if (!entry || !entry->in_flight || entry->operation != operation.type) return;
        entry->in_flight = false;
        if (operation.type == MqttSubscriptionOperationType::Subscribe) {
            entry->subscribe_attempted = true;
            entry->subscribe_failed = !success;
            entry->broker_subscribed = success;
            return;
        }

        if (success) {
            entry->broker_subscribed = false;
            entry->cleanup_failed = false;
            if (entry->owners == 0) clear(*entry);
        } else {
            ++entry->cleanup_attempts;
            entry->last_cleanup_ms = now_ms;
            entry->cleanup_failed = entry->cleanup_attempts >= kMaxCleanupAttempts;
        }
    }

    void recordDirectSubscribe(const char* topic, bool success) {
        if (!validTopic(topic)) return;
        Entry* entry = find(topic);
        if (!entry) entry = allocate(topic);
        if (!entry) return;
        entry->owners |= ownerBit(MqttSubscriptionOwner::Legacy);
        entry->subscribe_attempted = true;
        entry->subscribe_failed = !success;
        entry->broker_subscribed = success;
    }

    void disconnected() {
        for (Entry& entry : _entries) {
            if (!entry.used) continue;
            entry.broker_subscribed = false;
            entry.in_flight = false;
            entry.subscribe_attempted = false;
            entry.subscribe_failed = false;
            entry.cleanup_attempts = 0;
            entry.cleanup_failed = false;
            if (entry.owners == 0) clear(entry);
        }
    }

private:
    static constexpr uint8_t kMaxCleanupAttempts = 3;
    static constexpr uint32_t kCleanupRetryMs = 500;

    struct Entry {
        char topic[CONFIG_MQTT_TOPIC_MAX_LEN] = {};
        uint16_t owners = 0;
        uint8_t cleanup_attempts = 0;
        uint32_t last_cleanup_ms = 0;
        MqttSubscriptionOperationType operation = MqttSubscriptionOperationType::Subscribe;
        bool used = false;
        bool broker_subscribed = false;
        bool subscribe_attempted = false;
        bool subscribe_failed = false;
        bool cleanup_failed = false;
        bool in_flight = false;
    };

    static bool validTopic(const char* topic) {
        return topic && topic[0] && strnlen(topic, CONFIG_MQTT_TOPIC_MAX_LEN) < CONFIG_MQTT_TOPIC_MAX_LEN;
    }

    static uint16_t ownerBit(MqttSubscriptionOwner owner) {
        return static_cast<uint16_t>(1U << static_cast<uint8_t>(owner));
    }

    static void copyTopic(char* target, const char* source) {
        const size_t length = strlen(source);
        memcpy(target, source, length + 1);
    }

    Entry* find(const char* topic) {
        if (!validTopic(topic)) return nullptr;
        for (Entry& entry : _entries) {
            if (entry.used && strcmp(entry.topic, topic) == 0) return &entry;
        }
        return nullptr;
    }

    const Entry* find(const char* topic) const {
        if (!validTopic(topic)) return nullptr;
        for (const Entry& entry : _entries) {
            if (entry.used && strcmp(entry.topic, topic) == 0) return &entry;
        }
        return nullptr;
    }

    Entry* allocate(const char* topic) {
        for (Entry& entry : _entries) {
            if (!entry.used) {
                entry = {};
                entry.used = true;
                copyTopic(entry.topic, topic);
                return &entry;
            }
        }
        return nullptr;
    }

    static void clear(Entry& entry) {
        entry = {};
    }

    Entry _entries[kCapacity] = {};
};

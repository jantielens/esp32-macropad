#include "mqtt_sub_store_test_support.h"
#undef PAD_CONFIG_H
#include "pad_config.h"
#include "LittleFS.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "../src/app/mqtt_triggers.cpp"

MqttManager mqtt_manager;
FakeFontStorage LittleFS;
FakeFontStorage SD_MMC;

namespace {
bool allocation_fails = false;
bool mutex_fails = false;
unsigned allocations = 0;
unsigned mutex_creations = 0;
unsigned dispatches = 0;
std::vector<MqttSubStoreTestOwnerCall> owner_calls;

void expect_unlocked() {
    ASSERT_NE(g_lock, nullptr);
    EXPECT_TRUE(g_lock->signaled);
}
}

void* config_psram_alloc(size_t bytes, const char*) {
    ++allocations;
    expect_unlocked();
    return allocation_fails ? nullptr : calloc(1, bytes);
}

SemaphoreHandle_t test_freertos_create_mutex() {
    ++mutex_creations;
    if (mutex_fails) return nullptr;
    auto* semaphore = new StaticSemaphore_t{};
    semaphore->signaled = true;
    return semaphore;
}
void test_freertos_delete_mutex(SemaphoreHandle_t semaphore) { delete semaphore; }

bool storage_mount() { expect_unlocked(); return true; }
void storage_publish_usage(bool) { expect_unlocked(); }

void mqtt_sub_store_test_record_owner(const char* topic,
                                      MqttSubscriptionOwner owner, bool active) {
    expect_unlocked();
    MqttSubStoreTestOwnerCall call{};
    strlcpy(call.topic, topic, sizeof(call.topic));
    call.owner = owner;
    call.active = active;
    owner_calls.push_back(call);
}

uint8_t action_list_parse(JsonVariant value, ButtonAction* out, uint8_t max, bool) {
    memset(out, 0, sizeof(ButtonAction) * max);
    uint8_t count = 0;
    for (JsonObject action : value.as<JsonArray>()) {
        if (count == max) break;
        const char* type = action["type"] | "";
        if (*type) strlcpy(out[count++].type, type, sizeof(out[0].type));
    }
    return count;
}
void action_list_dispatch(const ButtonAction* actions, uint8_t count, const char*,
                          ActionContinuationOwner, bool) {
    expect_unlocked();
    if (count) {
        EXPECT_STREQ(actions[0].type, "screen");
        ++dispatches;
    }
}

class MqttTriggers : public testing::Test {
protected:
    void SetUp() override {
        allocation_fails = mutex_fails = false;
        allocations = mutex_creations = dispatches = 0;
        owner_calls.clear();
        LittleFS = {};
        LittleFS.read_hook = expect_unlocked;
    }
    void TearDown() override {
        free(g_triggers);
        g_triggers = nullptr;
        g_registered_topics = nullptr;
        delete g_lock;
        g_lock = nullptr;
        g_pending_mask = 0;
    }
    static void seed(const char* json) {
        LittleFS.files[MQTT_TRIGGERS_PATH] = std::vector<uint8_t>(json, json + strlen(json));
    }
    static bool save(const char* json) {
        return mqtt_triggers_save_raw(reinterpret_cast<const uint8_t*>(json), strlen(json));
    }
    static void expect_unavailable() {
        EXPECT_EQ(mqtt_triggers_get(0), nullptr);
        mqtt_triggers_on_connected();
        const uint8_t payload[] = {'o', 'n'};
        mqtt_triggers_on_message("old", payload, sizeof(payload));
        mqtt_triggers_loop();
        EXPECT_TRUE(owner_calls.empty());
        EXPECT_EQ(dispatches, 0U);
        EXPECT_TRUE(LittleFS.open_calls.empty());
    }
};

TEST_F(MqttTriggers, MutexFailureDoesNotPublishStorageAndRetrySucceeds) {
    mutex_fails = true;
    mqtt_triggers_init();
    expect_unavailable();
    EXPECT_EQ(allocations, 0U);
    mutex_fails = false;
    mqtt_triggers_init();
    EXPECT_NE(mqtt_triggers_get(0), nullptr);
    EXPECT_EQ(mutex_creations, 2U);
    EXPECT_EQ(allocations, 1U);
}

TEST_F(MqttTriggers, AllocationFailureLeavesUnavailableStateAndRetryReusesMutex) {
    allocation_fails = true;
    mqtt_triggers_init();
    expect_unavailable();
    allocation_fails = false;
    mqtt_triggers_init();
    EXPECT_NE(mqtt_triggers_get(0), nullptr);
    EXPECT_EQ(allocations, 2U);
    EXPECT_EQ(mutex_creations, 1U);
}

TEST_F(MqttTriggers, ReinitAndSaveKeepPointersAndReconcileRegisteredTopics) {
    seed(R"({"triggers":[{"topic":"old"},{"topic":"keep"}]})");
    mqtt_triggers_init();
    const auto* original = mqtt_triggers_get(0);
    ASSERT_NE(original, nullptr);
    mqtt_triggers_on_connected();
    owner_calls.clear();

    seed(R"({"triggers":[{"topic":"new"},{"topic":"keep"}]})");
    mqtt_triggers_init();
    EXPECT_EQ(mqtt_triggers_get(0), original);
    EXPECT_STREQ(original->topic, "new");
    EXPECT_EQ(allocations, 1U);
    EXPECT_EQ(mutex_creations, 1U);
    mqtt_triggers_on_connected();
    ASSERT_EQ(owner_calls.size(), 3U);
    EXPECT_STREQ(owner_calls[0].topic, "old");
    EXPECT_FALSE(owner_calls[0].active);
    EXPECT_STREQ(owner_calls[1].topic, "new");
    EXPECT_TRUE(owner_calls[1].active);
    EXPECT_STREQ(owner_calls[2].topic, "keep");
    EXPECT_TRUE(owner_calls[2].active);

    owner_calls.clear();
    ASSERT_TRUE(save(R"({"triggers":[{"topic":"keep"},{"topic":"added"}]})"));
    EXPECT_EQ(mqtt_triggers_get(0), original);
    EXPECT_STREQ(original->topic, "keep");
    ASSERT_EQ(owner_calls.size(), 3U);
    EXPECT_STREQ(owner_calls[0].topic, "new");
    EXPECT_FALSE(owner_calls[0].active);
    EXPECT_STREQ(owner_calls[1].topic, "keep");
    EXPECT_TRUE(owner_calls[1].active);
    EXPECT_STREQ(owner_calls[2].topic, "added");
    EXPECT_TRUE(owner_calls[2].active);
    owner_calls.clear();
    mqtt_triggers_on_connected();
    ASSERT_EQ(owner_calls.size(), 2U);
    EXPECT_TRUE(owner_calls[0].active);
    EXPECT_TRUE(owner_calls[1].active);
    EXPECT_EQ(allocations, 1U);
}

TEST_F(MqttTriggers, MatchingFanoutCoalescesAndDispatchesOutsideMutex) {
    seed(R"({"triggers":[{"topic":"event","value":"on","actions":[{"type":"screen"}]},
                           {"topic":"event","actions":[{"type":"screen"}]}]})");
    mqtt_triggers_init();
    const uint8_t off[] = {'o', 'f', 'f'};
    mqtt_triggers_on_message("event", off, sizeof(off));
    mqtt_triggers_loop();
    EXPECT_EQ(dispatches, 1U);
    const uint8_t on[] = {'o', 'n'};
    mqtt_triggers_on_message("event", on, sizeof(on));
    mqtt_triggers_on_message("event", on, sizeof(on));
    EXPECT_EQ(dispatches, 1U);
    mqtt_triggers_loop();
    EXPECT_EQ(dispatches, 3U);
    mqtt_triggers_loop();
    EXPECT_EQ(dispatches, 3U);
    EXPECT_EQ(mqtt_triggers_get(MAX_MQTT_TRIGGERS), nullptr);
}

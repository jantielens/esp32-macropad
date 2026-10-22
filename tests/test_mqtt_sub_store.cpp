#include "mqtt_sub_store.h"
#include "binding_sources.h"
#include "esp_heap_caps.h"

#include <gtest/gtest.h>

#include <string.h>

MqttManager mqtt_manager;

namespace {

MqttSubStoreTestOwnerCall g_owner_calls[8] = {};
size_t g_owner_call_count = 0;
#if HAS_DISPLAY
const char* g_saved_topic = nullptr;
#endif
bool g_workspace_fails = false;
size_t g_allocation_count = 0;
uint32_t g_allocation_caps = 0;

}

bool psramFound() { return HAS_PSRAM; }
void* heap_caps_malloc(size_t bytes, uint32_t caps) {
    ++g_allocation_count;
    g_allocation_caps = caps;
    return g_workspace_fails ? nullptr : malloc(bytes);
}
void heap_caps_free(void* memory) { free(memory); }

void mqtt_sub_store_test_record_owner(const char* topic,
                                      MqttSubscriptionOwner owner,
                                      bool active) {
    if (!topic || g_owner_call_count >= sizeof(g_owner_calls) / sizeof(g_owner_calls[0])) return;
    MqttSubStoreTestOwnerCall& call = g_owner_calls[g_owner_call_count++];
    strlcpy(call.topic, topic, sizeof(call.topic));
    call.owner = owner;
    call.active = active;
}

extern "C" void binding_template_collect_topics(const char* source, void* context) {
    mqtt_sub_store_collect_topic(context, source);
}

#if HAS_DISPLAY
void binding_sources_visit(BindingSourceVisitor visitor, void* context) {
    if (g_saved_topic) visitor(g_saved_topic, context);
}
#endif

#if HAS_DISPLAY
TEST(MqttSubStore, TracksReceivedStateAndPreservesPreviewOwnership) {
    constexpr const char* topic = "sensors/room/state";
    mqtt_sub_store_init();
    ASSERT_TRUE(mqtt_sub_store_active());
    ASSERT_TRUE(mqtt_sub_store_ensure_subscribed(topic));

    bool cached = true;
    ASSERT_TRUE(mqtt_sub_store_inspection_acquire(topic, &cached));
    EXPECT_FALSE(cached);
    ASSERT_TRUE(mqtt_sub_store_inspection_acquire(topic, &cached));
    EXPECT_FALSE(cached);

    uint8_t payload[16] = {};
    size_t payload_len = 99;
    bool received = true;
    bool truncated = true;
    ASSERT_TRUE(mqtt_sub_store_inspection_snapshot(
        topic, payload, sizeof(payload), &payload_len, &received, &truncated));
    EXPECT_FALSE(received);
    EXPECT_FALSE(truncated);
    EXPECT_EQ(payload_len, 0U);

    const uint8_t empty_payload = 0;
    mqtt_sub_store_set(topic, &empty_payload, 0);
    ASSERT_TRUE(mqtt_sub_store_inspection_snapshot(
        topic, payload, sizeof(payload), &payload_len, &received, &truncated));
    EXPECT_TRUE(received);
    EXPECT_FALSE(truncated);
    EXPECT_EQ(payload_len, 0U);
    EXPECT_EQ(payload[0], 0U);

    char value[16] = "stale";
    bool changed = false;
    ASSERT_TRUE(mqtt_sub_store_get(topic, value, sizeof(value), &changed));
    EXPECT_STREQ(value, "");
    EXPECT_TRUE(changed);
    mqtt_sub_store_clear_dirty();
    ASSERT_TRUE(mqtt_sub_store_get(topic, value, sizeof(value), &changed));
    EXPECT_FALSE(changed);

    ASSERT_TRUE(mqtt_sub_store_inspection_release(topic));
    EXPECT_EQ(g_owner_call_count, 2U);
    ASSERT_TRUE(mqtt_sub_store_inspection_release(topic));
    EXPECT_EQ(g_owner_call_count, 3U);
    EXPECT_EQ(g_owner_calls[0].owner, MqttSubscriptionOwner::Preview);
    EXPECT_TRUE(g_owner_calls[0].active);
    EXPECT_EQ(g_owner_calls[1].owner, MqttSubscriptionOwner::Inspection);
    EXPECT_TRUE(g_owner_calls[1].active);
    EXPECT_EQ(g_owner_calls[2].owner, MqttSubscriptionOwner::Inspection);
    EXPECT_FALSE(g_owner_calls[2].active);

    ASSERT_TRUE(mqtt_sub_store_get(topic, value, sizeof(value), &changed));
    EXPECT_STREQ(value, "");
    ASSERT_TRUE(mqtt_sub_store_inspection_acquire(topic, &cached));
    EXPECT_TRUE(cached);
    ASSERT_TRUE(mqtt_sub_store_inspection_release(topic));

    g_owner_call_count = 0;
    ASSERT_TRUE(mqtt_sub_store_inspection_acquire(topic, &cached));
    g_owner_call_count = 0;
    mqtt_sub_store_resubscribe();
    ASSERT_EQ(g_owner_call_count, 2U);
    EXPECT_STREQ(g_owner_calls[0].topic, topic);
    EXPECT_EQ(g_owner_calls[0].owner, MqttSubscriptionOwner::Preview);
    EXPECT_TRUE(g_owner_calls[0].active);
    EXPECT_STREQ(g_owner_calls[1].topic, topic);
    EXPECT_EQ(g_owner_calls[1].owner, MqttSubscriptionOwner::Inspection);
    EXPECT_TRUE(g_owner_calls[1].active);

    g_owner_call_count = 0;
    mqtt_sub_store_subscribe_all();
    ASSERT_EQ(g_owner_call_count, 1U);
    EXPECT_EQ(g_owner_calls[0].owner, MqttSubscriptionOwner::Preview);
    EXPECT_FALSE(g_owner_calls[0].active);
    ASSERT_TRUE(mqtt_sub_store_get(topic, value, sizeof(value), &changed));
    EXPECT_STREQ(value, "");

    g_owner_call_count = 0;
    mqtt_sub_store_resubscribe();
    ASSERT_EQ(g_owner_call_count, 1U);
    EXPECT_EQ(g_owner_calls[0].owner, MqttSubscriptionOwner::Inspection);
    EXPECT_TRUE(g_owner_calls[0].active);
    ASSERT_TRUE(mqtt_sub_store_inspection_release(topic));
    EXPECT_FALSE(mqtt_sub_store_get(topic, value, sizeof(value), &changed));
}

TEST(MqttSubStore, ReconcilesSavedPreviewAndInspectionInterests) {
    constexpr const char* topic = "sensors/saved/state";
    mqtt_sub_store_init();
    g_saved_topic = topic;
    g_owner_call_count = 0;
    mqtt_sub_store_subscribe_all();
    ASSERT_EQ(g_owner_call_count, 1U);
    EXPECT_EQ(g_owner_calls[0].owner, MqttSubscriptionOwner::SavedBindings);
    EXPECT_TRUE(g_owner_calls[0].active);

    mqtt_sub_store_ensure_subscribed(topic);
    ASSERT_TRUE(mqtt_sub_store_inspection_acquire(topic, nullptr));
    const uint8_t payload[] = "42";
    mqtt_sub_store_set(topic, payload, sizeof(payload) - 1);

    g_workspace_fails = true;
    g_owner_call_count = 0;
    const size_t allocation_count = g_allocation_count;
    mqtt_sub_store_subscribe_all();
    EXPECT_EQ(g_allocation_count, allocation_count + 1);
    EXPECT_EQ(g_allocation_caps,
              static_cast<uint32_t>(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    EXPECT_EQ(g_owner_call_count, 0U);
    g_workspace_fails = false;

    g_owner_call_count = 0;
    mqtt_sub_store_resubscribe();
    ASSERT_EQ(g_owner_call_count, 3U);
    EXPECT_EQ(g_owner_calls[0].owner, MqttSubscriptionOwner::SavedBindings);
    EXPECT_EQ(g_owner_calls[1].owner, MqttSubscriptionOwner::Preview);
    EXPECT_EQ(g_owner_calls[2].owner, MqttSubscriptionOwner::Inspection);
    for (size_t i = 0; i < g_owner_call_count; i++) {
        EXPECT_STREQ(g_owner_calls[i].topic, topic);
        EXPECT_TRUE(g_owner_calls[i].active);
    }

    g_owner_call_count = 0;
    mqtt_sub_store_subscribe_all();
    ASSERT_EQ(g_owner_call_count, 3U);
    EXPECT_EQ(g_owner_calls[0].owner, MqttSubscriptionOwner::SavedBindings);
    EXPECT_FALSE(g_owner_calls[0].active);
    EXPECT_EQ(g_owner_calls[1].owner, MqttSubscriptionOwner::Preview);
    EXPECT_FALSE(g_owner_calls[1].active);
    EXPECT_EQ(g_owner_calls[2].owner, MqttSubscriptionOwner::SavedBindings);
    EXPECT_TRUE(g_owner_calls[2].active);
    char value[16] = {};
    ASSERT_TRUE(mqtt_sub_store_get(topic, value, sizeof(value), nullptr));
    EXPECT_STREQ(value, "42");

    g_saved_topic = nullptr;
    g_owner_call_count = 0;
    mqtt_sub_store_subscribe_all();
    ASSERT_EQ(g_owner_call_count, 1U);
    EXPECT_EQ(g_owner_calls[0].owner, MqttSubscriptionOwner::SavedBindings);
    EXPECT_FALSE(g_owner_calls[0].active);
    ASSERT_TRUE(mqtt_sub_store_get(topic, value, sizeof(value), nullptr));
    EXPECT_STREQ(value, "42");
    ASSERT_TRUE(mqtt_sub_store_inspection_release(topic));
    EXPECT_FALSE(mqtt_sub_store_get(topic, value, sizeof(value), nullptr));
}
#else
TEST(MqttSubStore, DoesNotAllocateDisplayReconciliationWorkspace) {
    mqtt_sub_store_init();
    ASSERT_TRUE(mqtt_sub_store_active());

    const size_t allocations_before = g_allocation_count;
    mqtt_sub_store_subscribe_all();

    EXPECT_EQ(g_allocation_count, allocations_before);
}
#endif

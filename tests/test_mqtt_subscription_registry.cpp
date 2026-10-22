#include "mqtt_subscription_registry.h"

#include <gtest/gtest.h>

TEST(MqttSubscriptionRegistry, SharesExactFilterAcrossOwners) {
    MqttSubscriptionRegistry registry;
    constexpr const char* topic = "sensors/room/temperature";

    ASSERT_TRUE(registry.setOwner(topic, MqttSubscriptionOwner::SavedBindings, true));
    ASSERT_TRUE(registry.setOwner(topic, MqttSubscriptionOwner::Inspection, true));

    MqttSubscriptionOperation operation{};
    ASSERT_TRUE(registry.nextOperation(0, &operation));
    EXPECT_EQ(operation.type, MqttSubscriptionOperationType::Subscribe);
    EXPECT_STREQ(operation.topic, topic);
    registry.complete(operation, true, 0);
    EXPECT_EQ(registry.state(topic, MqttSubscriptionOwner::SavedBindings),
              MqttSubscriptionState::Accepted);
    EXPECT_EQ(registry.state(topic, MqttSubscriptionOwner::Inspection),
              MqttSubscriptionState::Accepted);

    ASSERT_TRUE(registry.setOwner(topic, MqttSubscriptionOwner::Inspection, false));
    EXPECT_EQ(registry.cleanup(topic), MqttSubscriptionCleanup::Complete);
    EXPECT_EQ(registry.state(topic, MqttSubscriptionOwner::SavedBindings),
              MqttSubscriptionState::Accepted);
    EXPECT_FALSE(registry.nextOperation(1, &operation));

    ASSERT_TRUE(registry.setOwner(topic, MqttSubscriptionOwner::SavedBindings, false));
    EXPECT_EQ(registry.cleanup(topic), MqttSubscriptionCleanup::Pending);
    ASSERT_TRUE(registry.nextOperation(1, &operation));
    EXPECT_EQ(operation.type, MqttSubscriptionOperationType::Unsubscribe);
    registry.complete(operation, true, 1);
    EXPECT_EQ(registry.cleanup(topic), MqttSubscriptionCleanup::Complete);
}

TEST(MqttSubscriptionRegistry, KeepsOverlappingFiltersIndependent) {
    MqttSubscriptionRegistry registry;
    ASSERT_TRUE(registry.setOwner("devices/+/state", MqttSubscriptionOwner::Triggers, true));
    ASSERT_TRUE(registry.setOwner("devices/kitchen/state", MqttSubscriptionOwner::Inspection, true));

    MqttSubscriptionOperation operation{};
    ASSERT_TRUE(registry.nextOperation(0, &operation));
    registry.complete(operation, true, 0);
    ASSERT_TRUE(registry.nextOperation(0, &operation));
    registry.complete(operation, true, 0);

    ASSERT_TRUE(registry.setOwner("devices/kitchen/state", MqttSubscriptionOwner::Inspection, false));
    ASSERT_TRUE(registry.nextOperation(1, &operation));
    EXPECT_STREQ(operation.topic, "devices/kitchen/state");
    EXPECT_EQ(operation.type, MqttSubscriptionOperationType::Unsubscribe);
    registry.complete(operation, true, 1);
    EXPECT_EQ(registry.state("devices/+/state", MqttSubscriptionOwner::Triggers),
              MqttSubscriptionState::Accepted);
}

TEST(MqttSubscriptionRegistry, ReestablishesCurrentOwnersAfterDisconnect) {
    MqttSubscriptionRegistry registry;
    constexpr const char* topic = "devices/pad/notify";
    ASSERT_TRUE(registry.setOwner(topic, MqttSubscriptionOwner::Notify, true));

    MqttSubscriptionOperation operation{};
    ASSERT_TRUE(registry.nextOperation(0, &operation));
    registry.complete(operation, true, 0);
    registry.disconnected();
    EXPECT_EQ(registry.state(topic, MqttSubscriptionOwner::Notify),
              MqttSubscriptionState::Pending);
    ASSERT_TRUE(registry.nextOperation(1, &operation));
    EXPECT_EQ(operation.type, MqttSubscriptionOperationType::Subscribe);
    registry.complete(operation, true, 1);
    EXPECT_EQ(registry.state(topic, MqttSubscriptionOwner::Notify),
              MqttSubscriptionState::Accepted);
}

TEST(MqttSubscriptionRegistry, ReportsBoundedUnsubscribeFailure) {
    MqttSubscriptionRegistry registry;
    constexpr const char* topic = "devices/pad/audio";
    ASSERT_TRUE(registry.setOwner(topic, MqttSubscriptionOwner::Audio, true));

    MqttSubscriptionOperation operation{};
    ASSERT_TRUE(registry.nextOperation(0, &operation));
    registry.complete(operation, true, 0);
    ASSERT_TRUE(registry.setOwner(topic, MqttSubscriptionOwner::Audio, false));

    for (uint32_t attempt = 0; attempt < 3; ++attempt) {
        ASSERT_TRUE(registry.nextOperation(attempt * 500, &operation));
        EXPECT_EQ(operation.type, MqttSubscriptionOperationType::Unsubscribe);
        registry.complete(operation, false, attempt * 500);
    }
    EXPECT_EQ(registry.cleanup(topic), MqttSubscriptionCleanup::Failed);
    EXPECT_FALSE(registry.nextOperation(5000, &operation));
}

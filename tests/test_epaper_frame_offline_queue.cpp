#include "device_classes/epaper_frame/epaper_frame_offline_queue_logic.h"

#include <gtest/gtest.h>
#include <limits.h>
#include <string.h>

namespace {

EpaperOfflineQueueEntry entry(const char* key, uint32_t crc,
		uint32_t length = 128) {
		EpaperOfflineQueueEntry value = {};
		strncpy(value.image_key, key, sizeof(value.image_key) - 1);
		strncpy(value.media_type, "application/vnd.photoframe.g16p",
				sizeof(value.media_type) - 1);
		value.content_crc32 = crc;
		value.content_length = length;
		return value;
}

EpaperOfflineWakeInputs valid_wake() {
		return {
				true, true, true, 5,
				true, false, true, true,
		};
}

} // namespace

TEST(EpaperOfflineQueue, EnforcesPairedOfflineAndBatchLimits) {
		uint8_t total = 0;
		EXPECT_TRUE(epaper_frame_offline_batch_count(0, &total));
		EXPECT_EQ(total, 1);
		EXPECT_TRUE(epaper_frame_offline_batch_count(16, &total));
		EXPECT_EQ(total, 17);
		EXPECT_FALSE(epaper_frame_offline_batch_count(17, &total));
		EXPECT_FALSE(epaper_frame_offline_batch_count(UINT32_MAX, &total));
		EXPECT_EQ(EPAPER_FRAME_OFFLINE_QUEUE_CAPACITY, 16);
		EXPECT_EQ(EPAPER_FRAME_BATCH_MAX_COUNT, 17);
}

TEST(EpaperOfflineQueue, SelectsOfflineOnlyForValidTimerWake) {
		EpaperOfflineWakeInputs inputs = valid_wake();
		EXPECT_EQ(epaper_frame_offline_wake_decision(inputs),
				EpaperOfflineWakeDecision::Offline);

		inputs.offline_refreshes = 0;
		EXPECT_EQ(epaper_frame_offline_wake_decision(inputs),
				EpaperOfflineWakeDecision::Online);
		inputs = valid_wake();
		inputs.queue_valid = false;
		EXPECT_EQ(epaper_frame_offline_wake_decision(inputs),
				EpaperOfflineWakeDecision::Online);
		inputs = valid_wake();
		inputs.button_wake = true;
		EXPECT_EQ(epaper_frame_offline_wake_decision(inputs),
				EpaperOfflineWakeDecision::Online);
		inputs = valid_wake();
		inputs.deep_sleep_wake = false;
		EXPECT_EQ(epaper_frame_offline_wake_decision(inputs),
				EpaperOfflineWakeDecision::Online);
		inputs = valid_wake();
		inputs.supported = false;
		EXPECT_EQ(epaper_frame_offline_wake_decision(inputs),
				EpaperOfflineWakeDecision::Online);
		inputs = valid_wake();
		inputs.service_mode = false;
		EXPECT_EQ(epaper_frame_offline_wake_decision(inputs),
				EpaperOfflineWakeDecision::Online);
		inputs = valid_wake();
		inputs.schedule_allowed = false;
		EXPECT_EQ(epaper_frame_offline_wake_decision(inputs),
				EpaperOfflineWakeDecision::ScheduleSuppressed);
}

TEST(EpaperOfflineQueue, TracksValidatedPrefixAndDequeuesAfterSuccessOnly) {
		EpaperOfflineQueueState state = {};
		epaper_frame_offline_queue_reset(&state, 42);
		EXPECT_TRUE(epaper_frame_offline_queue_state_valid(state, 42));
		EXPECT_TRUE(epaper_frame_offline_queue_append(&state, entry("one", 1)));
		EXPECT_TRUE(epaper_frame_offline_queue_append(&state, entry("two", 2)));
		EXPECT_EQ(state.count, 2);
		ASSERT_NE(epaper_frame_offline_queue_head(state), nullptr);
		EXPECT_STREQ(epaper_frame_offline_queue_head(state)->image_key, "one");

		EXPECT_FALSE(epaper_frame_offline_queue_complete_head(&state, false));
		EXPECT_EQ(state.count, 2);
		EXPECT_STREQ(epaper_frame_offline_queue_head(state)->image_key, "one");
		EXPECT_TRUE(epaper_frame_offline_queue_complete_head(&state, true));
		EXPECT_EQ(state.count, 1);
		EXPECT_STREQ(epaper_frame_offline_queue_head(state)->image_key, "two");
}

TEST(EpaperOfflineQueue, RejectsCorruptColdAndStaleState) {
		EpaperOfflineQueueState cold = {};
		EXPECT_FALSE(epaper_frame_offline_queue_state_valid(cold, 7));

		EpaperOfflineQueueState state = {};
		epaper_frame_offline_queue_reset(&state, 7);
		EXPECT_TRUE(epaper_frame_offline_queue_append(&state, entry("valid", 3)));
		EXPECT_FALSE(epaper_frame_offline_queue_state_valid(state, 8));
		state.entries[state.head].content_length = 0;
		EXPECT_FALSE(epaper_frame_offline_queue_state_valid(state, 7));
}

TEST(EpaperOfflineQueue, IdentityCoversQueueInvalidationInputs) {
		const uint32_t base = epaper_frame_offline_config_identity(
				1, "https://frame", "token", true, 5);
		EXPECT_NE(base, epaper_frame_offline_config_identity(
				0, "https://frame", "token", true, 5));
		EXPECT_NE(base, epaper_frame_offline_config_identity(
				1, "https://other", "token", true, 5));
		EXPECT_NE(base, epaper_frame_offline_config_identity(
				1, "https://frame", "other", true, 5));
		EXPECT_NE(base, epaper_frame_offline_config_identity(
				1, "https://frame", "token", false, 5));
		EXPECT_NE(base, epaper_frame_offline_config_identity(
				1, "https://frame", "token", true, 6));
}

TEST(EpaperOfflineQueue, ResetsTelemetryOnlyAfterSuccessfulPublish) {
		EpaperOfflineTelemetryAggregate aggregate = {};
		epaper_frame_offline_telemetry_record(&aggregate, 1, 2500, 3800,
				0x12345678, "rendered-image");
		EXPECT_EQ(aggregate.count, 1u);
		EXPECT_STREQ(aggregate.latest_image_key, "rendered-image");
		epaper_frame_offline_telemetry_publish_complete(&aggregate, false);
		EXPECT_EQ(aggregate.count, 1u);
		epaper_frame_offline_telemetry_publish_complete(&aggregate, true);
		EXPECT_EQ(aggregate.count, 0u);
		EXPECT_EQ(aggregate.latest_content_crc32, 0u);
		EXPECT_STREQ(aggregate.latest_image_key, "");
}

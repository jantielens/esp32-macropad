#include <gtest/gtest.h>

#include "device_classes/epaper/epaper_timing.h"

static void clear_wake_journal() {
		EpaperWakeRecord record = {};
		while (epaper_wake_journal_peek(&record)) {
			epaper_wake_journal_remove_oldest();
		}
		epaper_wake_journal_clear_dropped_count();
}

TEST(EpaperWakeJournal, KeepsCompletedWakesInOrder) {
		clear_wake_journal();
		epaper_timing_begin_wake(EpaperWakeReason::Timer);
		epaper_timing_last.boot_to_wifi_ms = 3200;
		epaper_wake_journal_checkpoint(EpaperWakeStage::Wifi);
		epaper_wake_journal_finalize(EpaperWakeResult::WifiFailed, 0, 0);

		epaper_timing_begin_wake(EpaperWakeReason::Button);
		epaper_timing_last.boot_to_wifi_ms = 3400;
		epaper_wake_journal_checkpoint(EpaperWakeStage::Refresh);
		epaper_wake_journal_finalize(EpaperWakeResult::Updated, 3850, 200);

		EpaperWakeRecord record = {};
		ASSERT_TRUE(epaper_wake_journal_peek(&record));
		EXPECT_EQ(record.wake_reason, EpaperWakeReason::Timer);
		EXPECT_EQ(record.last_stage, EpaperWakeStage::Wifi);
		EXPECT_EQ(record.result, EpaperWakeResult::WifiFailed);
		EXPECT_EQ(record.timing.boot_to_wifi_ms, 3200u);
		epaper_wake_journal_remove_oldest();

		ASSERT_TRUE(epaper_wake_journal_peek(&record));
		EXPECT_EQ(record.wake_reason, EpaperWakeReason::Button);
		EXPECT_EQ(record.result, EpaperWakeResult::Updated);
		EXPECT_EQ(record.battery_mv, 3850);
}

TEST(EpaperWakeJournal, PreservesRefreshResultOnDeliveryFailure) {
		clear_wake_journal();
		epaper_timing_begin_wake(EpaperWakeReason::Timer);
		epaper_wake_journal_finalize(EpaperWakeResult::Updated, 3840, 200);
		epaper_wake_journal_mark_delivery_failed(EpaperWakeResult::MqttPublishUnconfirmed);

		EpaperWakeRecord record = {};
		ASSERT_TRUE(epaper_wake_journal_peek(&record));
		EXPECT_EQ(record.result, EpaperWakeResult::MqttPublishUnconfirmed);
		EXPECT_EQ(record.refresh_result, EpaperWakeResult::Updated);
}

TEST(EpaperWakeJournal, DropsOldestRecordWhenFull) {
		clear_wake_journal();
		const uint32_t first_wake_id = epaper_timing_last.wake_id + 1;
		for (uint8_t index = 0; index <= EPAPER_WAKE_JOURNAL_CAPACITY; ++index) {
			epaper_timing_begin_wake(EpaperWakeReason::Timer);
			epaper_wake_journal_finalize(EpaperWakeResult::Updated, 3800, 200);
		}

		EpaperWakeRecord record = {};
		ASSERT_TRUE(epaper_wake_journal_peek(&record));
		EXPECT_EQ(record.timing.wake_id, first_wake_id + 1);
		EXPECT_EQ(epaper_wake_journal_dropped_count(), 1u);
}

TEST(EpaperWakeJournal, MarksUnfinishedPreviousWakeInterrupted) {
		clear_wake_journal();
		epaper_timing_begin_wake(EpaperWakeReason::Timer);
		epaper_timing_last.boot_to_wifi_ms = 3000;
		epaper_wake_journal_checkpoint(EpaperWakeStage::Wifi);
		epaper_timing_begin_wake(EpaperWakeReason::Timer);

		EpaperWakeRecord record = {};
		ASSERT_TRUE(epaper_wake_journal_peek(&record));
		EXPECT_EQ(record.result, EpaperWakeResult::Interrupted);
		EXPECT_EQ(record.last_stage, EpaperWakeStage::Wifi);
		EXPECT_EQ(record.timing.boot_to_wifi_ms, 3000u);
}

TEST(EpaperWakeJournal, KeepsSessionIdAcrossDeepSleepWakes) {
		clear_wake_journal();
		epaper_timing_begin_wake(EpaperWakeReason::Timer);
		const uint64_t session_id = epaper_timing_last.session_id;
		epaper_wake_journal_finalize(EpaperWakeResult::Updated, 3800, 200);

		epaper_timing_begin_wake(EpaperWakeReason::Timer);
		EXPECT_NE(session_id, 0u);
		EXPECT_EQ(epaper_timing_last.session_id, session_id);
		EXPECT_EQ(epaper_wake_journal_session_id(), session_id);
}

TEST(EpaperWakeJournal, IncludesPreviousDeliveryInNextWakeRecord) {
		clear_wake_journal();
		epaper_timing_begin_wake(EpaperWakeReason::Timer);
		const uint64_t session_id = epaper_timing_last.session_id;
		const uint32_t wake_id = epaper_timing_last.wake_id;
		epaper_wake_journal_complete_delivery(400, 125, 7200);
		epaper_wake_journal_finalize(EpaperWakeResult::Updated, 3800, 200);

		epaper_timing_begin_wake(EpaperWakeReason::Timer);
		EpaperWakeRecord record = {};
		epaper_wake_journal_remove_oldest();
		ASSERT_TRUE(epaper_wake_journal_peek(&record));
		ASSERT_TRUE(record.previous_delivery.valid);
		EXPECT_EQ(record.previous_delivery.session_id, session_id);
		EXPECT_EQ(record.previous_delivery.wake_id, wake_id);
		EXPECT_EQ(record.previous_delivery.mqtt_connect_ms, 400u);
		EXPECT_EQ(record.previous_delivery.mqtt_publish_ms, 125u);
		EXPECT_EQ(record.previous_delivery.total_active_ms, 7200u);
}

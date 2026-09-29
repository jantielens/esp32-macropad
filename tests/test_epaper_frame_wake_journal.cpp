#include <gtest/gtest.h>

#include "device_classes/epaper_frame/epaper_frame_timing.h"

void epaper_frame_wake_log_append(const EpaperWakeRecord&) {}

static void clear_wake_journal() {
		EpaperWakeRecord record = {};
		while (epaper_frame_wake_journal_peek(&record)) {
			epaper_frame_wake_journal_remove_oldest();
		}
		epaper_frame_wake_journal_clear_dropped_count();
}

TEST(EpaperWakeJournal, KeepsCompletedWakesInOrder) {
		clear_wake_journal();
		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		epaper_frame_timing_last.boot_to_wifi_ms = 3200;
		epaper_frame_wake_journal_checkpoint(EpaperWakeStage::Wifi);
		epaper_frame_wake_journal_finalize(EpaperWakeResult::WifiFailed, 0, 0);

		epaper_frame_timing_begin_wake(EpaperWakeReason::Button);
		epaper_frame_timing_last.boot_to_wifi_ms = 3400;
		epaper_frame_wake_journal_checkpoint(EpaperWakeStage::Refresh);
		epaper_frame_wake_journal_finalize(EpaperWakeResult::Updated, 3850, 200);

		EpaperWakeRecord record = {};
		ASSERT_TRUE(epaper_frame_wake_journal_peek(&record));
		EXPECT_EQ(record.wake_reason, EpaperWakeReason::Timer);
		EXPECT_EQ(record.last_stage, EpaperWakeStage::Wifi);
		EXPECT_EQ(record.result, EpaperWakeResult::WifiFailed);
		EXPECT_EQ(record.timing.boot_to_wifi_ms, 3200u);
		epaper_frame_wake_journal_remove_oldest();

		ASSERT_TRUE(epaper_frame_wake_journal_peek(&record));
		EXPECT_EQ(record.wake_reason, EpaperWakeReason::Button);
		EXPECT_EQ(record.result, EpaperWakeResult::Updated);
		EXPECT_EQ(record.battery_mv, 3850);
}

TEST(EpaperWakeJournal, PreservesRefreshResultOnDeliveryFailure) {
		clear_wake_journal();
		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		epaper_frame_wake_journal_finalize(EpaperWakeResult::Updated, 3840, 200);
		epaper_frame_wake_journal_mark_delivery_failed(EpaperWakeResult::MqttPublishUnconfirmed);

		EpaperWakeRecord record = {};
		ASSERT_TRUE(epaper_frame_wake_journal_peek(&record));
		EXPECT_EQ(record.result, EpaperWakeResult::MqttPublishUnconfirmed);
		EXPECT_EQ(record.refresh_result, EpaperWakeResult::Updated);
}

TEST(EpaperWakeJournal, RecordsBatchRequestsPerWake) {
		clear_wake_journal();
		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		epaper_frame_timing_record_batch_request(120);
		epaper_frame_timing_record_batch_request(300);
		epaper_frame_timing_last.batch_manifest_status = EpaperBatchManifestStatus::FailedFetch;
			epaper_frame_timing_last.batch_manifest_http_ms = 5000;
			epaper_frame_timing_last.batch_manifest_http_code = -1;
		epaper_frame_timing_last.batch_fallback_used = true;
			epaper_frame_timing_last.fallback_elapsed_ms = 130;
			epaper_frame_timing_last.fallback_http_code = 200;
		epaper_frame_wake_journal_finalize(EpaperWakeResult::Updated, 3800, 0);

		EpaperWakeRecord record = {};
		ASSERT_TRUE(epaper_frame_wake_journal_peek(&record));
		EXPECT_EQ(record.timing.batch_http_ms, 420u);
		EXPECT_EQ(record.timing.batch_slowest_request_ms, 300u);
		EXPECT_EQ(record.timing.batch_manifest_status, EpaperBatchManifestStatus::FailedFetch);
			EXPECT_EQ(record.timing.batch_manifest_http_ms, 5000u);
			EXPECT_EQ(record.timing.batch_manifest_http_code, -1);
		EXPECT_TRUE(record.timing.batch_fallback_used);
			EXPECT_EQ(record.timing.fallback_elapsed_ms, 130u);
			EXPECT_EQ(record.timing.fallback_http_code, 200);

		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		EXPECT_EQ(epaper_frame_timing_last.batch_http_ms, 0u);
		EXPECT_EQ(epaper_frame_timing_last.batch_slowest_request_ms, 0u);
		EXPECT_EQ(epaper_frame_timing_last.batch_manifest_status, EpaperBatchManifestStatus::NotAttempted);
			EXPECT_EQ(epaper_frame_timing_last.batch_manifest_http_ms, 0u);
			EXPECT_EQ(epaper_frame_timing_last.batch_manifest_http_code, 0);
		EXPECT_FALSE(epaper_frame_timing_last.batch_fallback_used);
			EXPECT_EQ(epaper_frame_timing_last.fallback_elapsed_ms, 0u);
			EXPECT_EQ(epaper_frame_timing_last.fallback_http_code, 0);
}

TEST(EpaperWakeJournal, ReportsPreviousSleepRequestOnNextWake) {
		clear_wake_journal();
		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		epaper_frame_wake_journal_finalize(EpaperWakeResult::Updated, 3800, 0);
		epaper_frame_timing_record_sleep_request(0);

		epaper_frame_timing_begin_wake(EpaperWakeReason::Button);
		EXPECT_TRUE(epaper_frame_timing_last.previous_sleep_requested);
		EXPECT_EQ(epaper_frame_timing_last.previous_requested_sleep_s, 0u);
		epaper_frame_wake_journal_finalize(EpaperWakeResult::Skipped, 3800, 0);
		epaper_frame_timing_record_sleep_request(600);

		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		EpaperWakeRecord record = {};
		epaper_frame_wake_journal_remove_oldest();
		epaper_frame_wake_journal_remove_oldest();
		ASSERT_TRUE(epaper_frame_wake_journal_peek(&record));
		EXPECT_TRUE(record.timing.previous_sleep_requested);
		EXPECT_EQ(record.timing.previous_requested_sleep_s, 600u);
		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		EXPECT_FALSE(epaper_frame_timing_last.previous_sleep_requested);
}

TEST(EpaperWakeJournal, PreservesRefreshResultOnBudgetCut) {
		clear_wake_journal();
		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		epaper_frame_wake_journal_finalize(EpaperWakeResult::Updated, 3840, 200);
		epaper_frame_timing_last.overall_budget_ms = 10000;
		epaper_frame_timing_last.budget_cut = 3;
		epaper_frame_wake_journal_checkpoint(EpaperWakeStage::MqttConnect);
		epaper_frame_wake_journal_mark_delivery_failed(EpaperWakeResult::BudgetExceeded);

		EpaperWakeRecord record = {};
		ASSERT_TRUE(epaper_frame_wake_journal_peek(&record));
		EXPECT_EQ(record.result, EpaperWakeResult::BudgetExceeded);
		EXPECT_EQ(record.refresh_result, EpaperWakeResult::Updated);
		EXPECT_EQ(record.timing.overall_budget_ms, 10000u);
		EXPECT_EQ(record.timing.budget_cut, 3u);
}

TEST(EpaperWakeJournal, DropsOldestRecordWhenFull) {
		clear_wake_journal();
		const uint32_t first_wake_id = epaper_frame_timing_last.wake_id + 1;
		for (uint8_t index = 0; index <= EPAPER_FRAME_WAKE_JOURNAL_CAPACITY; ++index) {
			epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
			epaper_frame_wake_journal_finalize(EpaperWakeResult::Updated, 3800, 200);
		}

		EpaperWakeRecord record = {};
		ASSERT_TRUE(epaper_frame_wake_journal_peek(&record));
		EXPECT_EQ(record.timing.wake_id, first_wake_id + 1);
		EXPECT_EQ(epaper_frame_wake_journal_dropped_count(), 1u);
}

TEST(EpaperWakeJournal, MarksUnfinishedPreviousWakeInterrupted) {
		clear_wake_journal();
		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		epaper_frame_timing_last.boot_to_wifi_ms = 3000;
		epaper_frame_wake_journal_checkpoint(EpaperWakeStage::Wifi);
		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);

		EpaperWakeRecord record = {};
		ASSERT_TRUE(epaper_frame_wake_journal_peek(&record));
		EXPECT_EQ(record.result, EpaperWakeResult::Interrupted);
		EXPECT_EQ(record.last_stage, EpaperWakeStage::Wifi);
		EXPECT_EQ(record.timing.boot_to_wifi_ms, 3000u);
}

TEST(EpaperWakeJournal, KeepsSessionIdAcrossDeepSleepWakes) {
		clear_wake_journal();
		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		const uint64_t session_id = epaper_frame_timing_last.session_id;
		epaper_frame_wake_journal_finalize(EpaperWakeResult::Updated, 3800, 200);

		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		EXPECT_NE(session_id, 0u);
		EXPECT_EQ(epaper_frame_timing_last.session_id, session_id);
		EXPECT_EQ(epaper_frame_wake_journal_session_id(), session_id);
}

TEST(EpaperWakeJournal, IncludesPreviousDeliveryInNextWakeRecord) {
		clear_wake_journal();
		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		const uint64_t session_id = epaper_frame_timing_last.session_id;
		const uint32_t wake_id = epaper_frame_timing_last.wake_id;
		epaper_frame_wake_journal_complete_delivery(400, 125, 7200);
		epaper_frame_wake_journal_finalize(EpaperWakeResult::Updated, 3800, 200);

		epaper_frame_timing_begin_wake(EpaperWakeReason::Timer);
		EpaperWakeRecord record = {};
		epaper_frame_wake_journal_remove_oldest();
		ASSERT_TRUE(epaper_frame_wake_journal_peek(&record));
		ASSERT_TRUE(record.previous_delivery.valid);
		EXPECT_EQ(record.previous_delivery.session_id, session_id);
		EXPECT_EQ(record.previous_delivery.wake_id, wake_id);
		EXPECT_EQ(record.previous_delivery.mqtt_connect_ms, 400u);
		EXPECT_EQ(record.previous_delivery.mqtt_publish_ms, 125u);
		EXPECT_EQ(record.previous_delivery.total_active_ms, 7200u);
}

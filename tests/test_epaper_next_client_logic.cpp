#include "device_classes/epaper/epaper_next_client_logic.h"
#include "device_classes/epaper/epaper_transport_crc32.h"

#include <gtest/gtest.h>
#include <stdint.h>

TEST(EpaperNextClientLogic, SelectsSourceActionsAndValidatesTransportValues) {
		EXPECT_FALSE(epaper_source_uses_service(EpaperSourceMode::SlotCarousel));
		EXPECT_TRUE(epaper_source_uses_service(EpaperSourceMode::Service));
		EXPECT_TRUE(epaper_source_advances_carousel(EpaperSourceMode::SlotCarousel, 2));
		EXPECT_FALSE(epaper_source_advances_carousel(EpaperSourceMode::Service, 2));
		EXPECT_EQ(epaper_source_refresh_interval(EpaperSourceMode::SlotCarousel, 60, 900), 900u);
		EXPECT_EQ(epaper_source_refresh_interval(EpaperSourceMode::Service, 60, 900), 60u);

		EXPECT_EQ(epaper_next_action_for_status(200), EpaperNextAction::DownloadInline);
		EXPECT_EQ(epaper_next_action_for_status(204), EpaperNextAction::Keep);
		EXPECT_EQ(epaper_next_action_for_status(302), EpaperNextAction::FollowRedirect);
		EXPECT_EQ(epaper_next_action_for_status(401), EpaperNextAction::AuthFailed);
		EXPECT_EQ(epaper_next_action_for_status(404), EpaperNextAction::UnsupportedMajor);
		EXPECT_EQ(epaper_next_action_for_status(503), EpaperNextAction::TransientFailure);
		EXPECT_EQ(epaper_next_action_for_status(301), EpaperNextAction::InvalidResponse);
		EXPECT_EQ(epaper_next_retry_decision(EpaperNextResult::Show), EpaperRetryDecision::Draw);
		EXPECT_EQ(epaper_next_retry_decision(EpaperNextResult::Keep), EpaperRetryDecision::Skip);
		EXPECT_EQ(epaper_next_retry_decision(EpaperNextResult::FailedFetch), EpaperRetryDecision::Fail);
		EXPECT_TRUE(epaper_next_use_cached_blob(true, true));
		EXPECT_FALSE(epaper_next_use_cached_blob(false, true));

		uint32_t crc = 1;
		EXPECT_TRUE(epaper_next_parse_crc32("00000000", &crc)); EXPECT_EQ(crc, 0u);
		EXPECT_TRUE(epaper_next_parse_crc32("89abcdef", &crc)); EXPECT_EQ(crc, 0x89ABCDEFU);
		EXPECT_FALSE(epaper_next_parse_crc32("89ABCDEF", &crc));
		EXPECT_FALSE(epaper_next_parse_crc32("1234567", &crc));
		EXPECT_TRUE(epaper_next_valid_image_key("M7x4qQ2V0A"));
		EXPECT_FALSE(epaper_next_valid_image_key("bad/key"));

		const uint8_t vector[] = "123456789";
		EXPECT_EQ(epaper_transport_crc32(nullptr, 0), 0u);
		EXPECT_EQ(epaper_transport_crc32(vector, 9), 0xCBF43926U);
}
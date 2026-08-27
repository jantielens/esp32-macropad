#include <gtest/gtest.h>
#include <stdint.h>
#include <time.h>

#include "device_classes/epaper/epaper_schedule.h"

class EpaperScheduleTest : public ::testing::Test {
protected:
		// 2000-01-01 00:00:00 UTC = 946684800
		static constexpr uint64_t EPOCH_2000_01_01_00_00 = 946684800;

		// 2000-01-01 12:00:00 UTC = epoch + 43200
		static constexpr uint64_t EPOCH_2000_01_01_12_00 = 946684800 + 43200;

		// 1999-12-31 23:59:59 UTC (before MIN_VALID_EPOCH)
		static constexpr uint64_t EPOCH_1999_STALE = 946684799;

		void SetUp() override {
				// No setup needed for pure functions
		}
};

constexpr uint64_t EpaperScheduleTest::EPOCH_2000_01_01_00_00;
constexpr uint64_t EpaperScheduleTest::EPOCH_2000_01_01_12_00;
constexpr uint64_t EpaperScheduleTest::EPOCH_1999_STALE;

// Test: fail-open on stale clock
TEST_F(EpaperScheduleTest, FailOpenOnStaleClock) {
		// If clock hasn't synced yet, should refresh regardless of schedule
		EXPECT_TRUE(epaper_schedule_should_refresh(0x0, 0, EPOCH_1999_STALE));  // all disabled, but clock stale
		EXPECT_TRUE(epaper_schedule_should_refresh(0x0, 0, EPOCH_1999_STALE - 1000));
}

// Test: hour 0 (midnight UTC) enabled
TEST_F(EpaperScheduleTest, HourZeroEnabledAtMidnight) {
		// Bit 0 set = hour 0 enabled
		uint32_t bitmask = (1U << 0);
		// At midnight UTC with tz=0, hour should be 0
		EXPECT_TRUE(epaper_schedule_should_refresh(bitmask, 0, EPOCH_2000_01_01_00_00));
}

// Test: hour 23 (11pm UTC) enabled
TEST_F(EpaperScheduleTest, HourTwentyThreeEnabledAt11pm) {
		// Bit 23 set = hour 23 enabled
		uint32_t bitmask = (1U << 23);
		// 23 hours after midnight UTC
		uint64_t epoch_23_00_utc = EPOCH_2000_01_01_00_00 + (23 * 3600);
		EXPECT_TRUE(epaper_schedule_should_refresh(bitmask, 0, epoch_23_00_utc));
}

// Test: hour disabled
TEST_F(EpaperScheduleTest, HourDisabledReturnsFalse) {
		// Only hours 0 and 1 enabled
		uint32_t bitmask = (1U << 0) | (1U << 1);
		// 12:00 UTC = hour 12, which is not enabled
		EXPECT_FALSE(epaper_schedule_should_refresh(bitmask, 0, EPOCH_2000_01_01_12_00));
}

// Test: timezone offset shifts hours correctly (positive offset)
TEST_F(EpaperScheduleTest, TimezoneOffsetPositive) {
		// Only hour 0 (local) enabled
		uint32_t bitmask = (1U << 0);
		// UTC noon (12:00 UTC) with tz+5 = 17:00 local (hour 17)
		uint64_t epoch_12_00_utc = EPOCH_2000_01_01_00_00 + (12 * 3600);
		EXPECT_FALSE(epaper_schedule_should_refresh(bitmask, 5, epoch_12_00_utc));

		// But hour 17 enabled should work
		bitmask = (1U << 17);
		EXPECT_TRUE(epaper_schedule_should_refresh(bitmask, 5, epoch_12_00_utc));
}

// Test: timezone offset shifts hours correctly (negative offset)
TEST_F(EpaperScheduleTest, TimezoneOffsetNegative) {
		// Only hour 10 (local) enabled
		uint32_t bitmask = (1U << 10);
		// UTC noon (12:00 UTC) with tz-5 = 07:00 local (hour 7)
		uint64_t epoch_12_00_utc = EPOCH_2000_01_01_00_00 + (12 * 3600);
		EXPECT_FALSE(epaper_schedule_should_refresh(bitmask, -5, epoch_12_00_utc));

		// But hour 7 enabled should work
		bitmask = (1U << 7);
		EXPECT_TRUE(epaper_schedule_should_refresh(bitmask, -5, epoch_12_00_utc));
}

// Test: timezone wraparound (negative offset at midnight)
TEST_F(EpaperScheduleTest, TimezoneWrapAroundNegative) {
		// UTC midnight with tz-5 = previous day 19:00 (hour 19)
		uint32_t bitmask = (1U << 19);
		EXPECT_TRUE(epaper_schedule_should_refresh(bitmask, -5, EPOCH_2000_01_01_00_00));
}

// Test: timezone wraparound (positive offset at late hours)
TEST_F(EpaperScheduleTest, TimezoneWrapAroundPositive) {
		// UTC 23:00 with tz+5 = 04:00 next day (hour 4)
		uint64_t epoch_23_00_utc = EPOCH_2000_01_01_00_00 + (23 * 3600);
		uint32_t bitmask = (1U << 4);
		EXPECT_TRUE(epaper_schedule_should_refresh(bitmask, 5, epoch_23_00_utc));
}

// Test: all hours disabled (bitmask=0) returns false
TEST_F(EpaperScheduleTest, AllHoursDisabled) {
		EXPECT_FALSE(epaper_schedule_should_refresh(0x0, 0, EPOCH_2000_01_01_12_00));
}

// Test: all hours enabled (bitmask=0xFFFFFF)
TEST_F(EpaperScheduleTest, AllHoursEnabled) {
		EXPECT_TRUE(epaper_schedule_should_refresh(0xFFFFFF, 0, EPOCH_2000_01_01_12_00));
}

// Test: seconds_to_next when all hours disabled (mask=0)
TEST_F(EpaperScheduleTest, SecondsToNextAllDisabledReturn3600) {
		uint32_t secs = epaper_schedule_seconds_to_next(0x0, 0, EPOCH_2000_01_01_12_00);
		EXPECT_EQ(secs, 3600);
}

// Test: seconds_to_next finds next enabled hour
TEST_F(EpaperScheduleTest, SecondsToNextFindNextEnabledHour) {
		// At hour 10, next enabled hour is 12 (2 hours away)
		uint64_t epoch_10_00 = EPOCH_2000_01_01_00_00 + (10 * 3600);
		uint32_t bitmask = (1U << 12);  // Only hour 12 enabled
		uint32_t secs = epaper_schedule_seconds_to_next(bitmask, 0, epoch_10_00);
		// Should be approximately 2 hours = 7200 seconds
		// Exact value depends on whether we're at start of hour or not
		EXPECT_GE(secs, 7200 - 60);  // Allow small variance
		EXPECT_LE(secs, 7200 + 60);
}

// Test: seconds_to_next wraps to next day
TEST_F(EpaperScheduleTest, SecondsToNextWrapsToNextDay) {
		// At hour 23, next enabled hour is 1 (2 hours away, wrapping day)
		uint64_t epoch_23_00 = EPOCH_2000_01_01_00_00 + (23 * 3600);
		uint32_t bitmask = (1U << 1);  // Only hour 1 enabled
		uint32_t secs = epaper_schedule_seconds_to_next(bitmask, 0, epoch_23_00);
		// Should be approximately 2 hours = 7200 seconds
		EXPECT_GE(secs, 7200 - 60);
		EXPECT_LE(secs, 7200 + 60);
}

// Test: seconds_to_next with timezone offset
TEST_F(EpaperScheduleTest, SecondsToNextWithTimezoneOffset) {
		// At UTC 12:00 with tz+5 = local 17:00 (hour 17)
		// Next enabled hour is 19 (local) = 14:00 UTC = 2 hours later
		uint64_t epoch_12_00_utc = EPOCH_2000_01_01_00_00 + (12 * 3600);
		uint32_t bitmask = (1U << 19);  // Hour 19 local enabled
		uint32_t secs = epaper_schedule_seconds_to_next(bitmask, 5, epoch_12_00_utc);
		EXPECT_GE(secs, 7200 - 60);
		EXPECT_LE(secs, 7200 + 60);
}

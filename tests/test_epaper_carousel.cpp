#include <gtest/gtest.h>
#include <stdint.h>

#include "device_classes/epaper/epaper_carousel.h"

class EpaperCarouselTest : public ::testing::Test {
protected:
		void SetUp() override {
				// No setup needed for pure functions
		}
};

// Test: advancing through a 5-entry carousel
TEST_F(EpaperCarouselTest, NextIndexWrapsAt5Entries) {
		EXPECT_EQ(epaper_carousel_next_index(0, 5, false), 1);
		EXPECT_EQ(epaper_carousel_next_index(1, 5, false), 2);
		EXPECT_EQ(epaper_carousel_next_index(2, 5, false), 3);
		EXPECT_EQ(epaper_carousel_next_index(3, 5, false), 4);
		EXPECT_EQ(epaper_carousel_next_index(4, 5, false), 0);  // wraparound
}

// Test: stay flag pauses rotation
TEST_F(EpaperCarouselTest, StayFlagPausesRotation) {
		EXPECT_EQ(epaper_carousel_next_index(0, 5, true), 0);
		EXPECT_EQ(epaper_carousel_next_index(2, 5, true), 2);
		EXPECT_EQ(epaper_carousel_next_index(4, 5, true), 4);
}

// Test: single-entry carousel always returns 0
TEST_F(EpaperCarouselTest, SingleEntryContinuesAtZero) {
		EXPECT_EQ(epaper_carousel_next_index(0, 1, false), 0);
		EXPECT_EQ(epaper_carousel_next_index(0, 1, true), 0);
}

// Test: empty carousel (count=0) returns 0
TEST_F(EpaperCarouselTest, EmptyCarouselReturnsZero) {
		EXPECT_EQ(epaper_carousel_next_index(0, 0, false), 0);
}

// Test: two-entry carousel
TEST_F(EpaperCarouselTest, TwoEntryCycle) {
		EXPECT_EQ(epaper_carousel_next_index(0, 2, false), 1);
		EXPECT_EQ(epaper_carousel_next_index(1, 2, false), 0);
}

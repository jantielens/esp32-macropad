#include "power_config.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>

TEST(PowerConfig, ParsesFullEpaperFrameMode) {
    constexpr char kFrameMode[] = "duty_cycle_epaper_frame";
    DeviceConfig config = {};

    ASSERT_GT(sizeof(config.operating_mode), std::strlen(kFrameMode));
    std::snprintf(config.operating_mode, sizeof(config.operating_mode), "%s", kFrameMode);

    EXPECT_STREQ(config.operating_mode, kFrameMode);
    EXPECT_EQ(power_config_parse_power_mode(&config), PowerMode::DutyCycleEpaperFrame);
}
#include <gtest/gtest.h>

#include "button_shadow_color.h"

TEST(ButtonShadowColor, DarkensRgbChannelsByPercent) {
    EXPECT_EQ(button_shadow_darken_color(0x336699, 0), 0x336699u);
    EXPECT_EQ(button_shadow_darken_color(0x336699, 50), 0x19334Cu);
    EXPECT_EQ(button_shadow_darken_color(0x336699, 100), 0x000000u);
}
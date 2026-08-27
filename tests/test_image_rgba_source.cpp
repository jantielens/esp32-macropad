#include <gtest/gtest.h>
#include <stdint.h>

#include "image_rgba_source.h"

TEST(ImageRgbaSource, ValidatesStrideAndCalculatesPixels) {
    static const size_t width = 2;
    static const size_t height = 2;
    static const size_t stride = 12;
    uint8_t pixels[stride * height] = {
        1, 2, 3, 4, 5, 6, 7, 8, 0xEE, 0xEE, 0xEE, 0xEE,
        9, 10, 11, 12, 13, 14, 15, 16, 0xDD, 0xDD, 0xDD, 0xDD,
    };

    EXPECT_TRUE(image_rgba_source_is_valid(width, height, stride, sizeof(pixels)));
    EXPECT_EQ(image_rgba_source_pixel(pixels, 0, stride, 1)[0], 5);
    EXPECT_EQ(image_rgba_source_pixel(pixels, 1, stride, 0)[0], 9);
    EXPECT_EQ(image_rgba_source_pixel(pixels, 1, stride, 1)[2], 15);

    EXPECT_FALSE(image_rgba_source_is_valid(width, height, width * 4 - 1, sizeof(pixels)));
    EXPECT_FALSE(image_rgba_source_is_valid(width, height, stride, sizeof(pixels) - 1));
    EXPECT_FALSE(image_rgba_source_is_valid(0, height, stride, sizeof(pixels)));
    EXPECT_FALSE(image_rgba_source_is_valid(width, 0, stride, sizeof(pixels)));
    EXPECT_FALSE(image_rgba_source_is_valid(UINT32_MAX, UINT32_MAX, SIZE_MAX, SIZE_MAX));
}
#include "image_decoder.h"
#include <lvgl.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#define LODEPNG_NO_COMPILE_CPP
#include <libs/lodepng/lodepng.h>

void log_noop(const char*, const char*, ...) {}

static void check_stripes(unsigned width, unsigned height, ImageScaleMode mode,
                          uint16_t expected) {
    std::vector<uint8_t> source(width * height * 4, 255);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const unsigned band = (width > height ? x : y) / 2;
            auto* pixel = &source[(y * width + x) * 4];
            pixel[0] = band == 0 ? 255 : 0;
            pixel[1] = band == 1 ? 255 : 0;
            pixel[2] = band == 2 ? 255 : 0;
        }
    }
    uint8_t* png = nullptr;
    size_t png_size = 0;
    assert(lodepng_encode32(&png, &png_size, source.data(), width, height) == 0);
    uint16_t* output = nullptr;
    size_t output_size = 0;
    assert(image_decode_to_rgb565(png, png_size, 2, 2, mode, &output, &output_size));
    assert(output_size == 8);
    for (unsigned index = 0; index < 4; ++index) assert(output[index] == expected);
    free(output);

    assert(image_decode_to_rgb565(png, png_size, 4, 4, mode, &output, &output_size));
    assert(output_size == 32);
    assert(output[0] == expected);
    free(output);

    if (mode == IMAGE_SCALE_COVER) {
        assert(image_decode_to_rgb565(png, png_size, 6, 6, IMAGE_SCALE_LETTERBOX,
                                      &output, &output_size, 0x1234));
        assert(output_size == 72);
        assert(output[0] == 0x1234);
        assert(output[3 * 6 + 3] == 0x07E0);
        assert(output[35] == 0x1234);
        free(output);
    }
    lv_free(png);
}

int main() {
    lv_init();
    check_stripes(2, 6, IMAGE_SCALE_COVER_TOP, 0xF800);
    check_stripes(2, 6, IMAGE_SCALE_COVER, 0x07E0);
    check_stripes(2, 6, IMAGE_SCALE_COVER_BOTTOM, 0x001F);
    check_stripes(6, 2, IMAGE_SCALE_COVER_LEFT, 0xF800);
    check_stripes(6, 2, IMAGE_SCALE_COVER, 0x07E0);
    check_stripes(6, 2, IMAGE_SCALE_COVER_RIGHT, 0x001F);
    check_stripes(2, 6, IMAGE_SCALE_COVER_LEFT, 0x07E0);
    check_stripes(6, 2, IMAGE_SCALE_COVER_TOP, 0x07E0);
    check_stripes(2, 2, IMAGE_SCALE_COVER_BOTTOM, 0xF800);
    puts("image_decoder: PASS");
}

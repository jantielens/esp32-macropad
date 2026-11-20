#include "image_decoder.h"
#include "screens/pad_image_frame.h"
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
    ImageContentBounds content = {};
    assert(image_decode_to_rgb565(png, png_size, 2, 2, mode, &output, &output_size, 0, &content));
    assert(content.x == 0 && content.y == 0 && content.width == 2 && content.height == 2);
    assert(output_size == 8);
    for (unsigned index = 0; index < 4; ++index) assert(output[index] == expected);
    free(output);

    assert(image_decode_to_rgb565(png, png_size, 4, 4, mode, &output, &output_size));
    assert(output_size == 32);
    assert(output[0] == expected);
    free(output);

    if (mode == IMAGE_SCALE_COVER) {
        assert(image_decode_to_rgb565(png, png_size, 6, 6, IMAGE_SCALE_LETTERBOX,
                                      &output, &output_size, 0x1234, &content));
        assert(content.x == (width > height ? 0 : 2));
        assert(content.y == (width > height ? 2 : 0));
        assert(content.width == (width > height ? 6 : 2));
        assert(content.height == (width > height ? 2 : 6));
        assert(output_size == 72);
        assert(output[0] == 0x1234);
        assert(output[3 * 6 + 3] == 0x07E0);
        assert(output[35] == 0x1234);
        free(output);
    }
    lv_free(png);
}

static void check_transparent_letterbox() {
    auto* display = lv_display_create(6, 6);
    uint16_t draw_buffer[36] = {};
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, nullptr, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, [](lv_display_t* output, const lv_area_t*, uint8_t*) {
        lv_display_flush_ready(output);
    });
    auto* screen = lv_display_get_screen_active(display);
    auto* underlying_button = lv_obj_create(screen);
    lv_obj_remove_style_all(underlying_button);
    lv_obj_set_size(underlying_button, 6, 6);
    lv_obj_set_style_bg_color(underlying_button, lv_color_hex(0x0000ff), 0);
    lv_obj_set_style_bg_opa(underlying_button, LV_OPA_COVER, 0);
    auto* button = lv_obj_create(screen);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 6, 6);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    auto* image = lv_image_create(button);
    lv_obj_set_align(image, LV_ALIGN_CENTER);
    lv_image_dsc_t descriptor = {};

    for (const auto& bounds : {ImageContentBounds{2, 0, 2, 6}, ImageContentBounds{0, 2, 6, 2},
                               ImageContentBounds{0, 1, 6, 3}, ImageContentBounds{1, 0, 3, 6},
                               ImageContentBounds{0, 0, 6, 6}}) {
        uint16_t source[36];
        for (unsigned y = 0; y < 6; ++y) {
            for (unsigned x = 0; x < 6; ++x) {
                const bool inside = x >= bounds.x && x < bounds.x + bounds.width &&
                                    y >= bounds.y && y < bounds.y + bounds.height;
                source[y * 6 + x] = inside ? 0x07E0 : 0xF800;
            }
        }
        for (bool transparent : {true, false, true}) {
            pad_set_image_frame(image, descriptor, source, 6, 6, bounds, transparent);
            lv_refr_now(display);
            for (unsigned y = 0; y < 6; ++y) {
                for (unsigned x = 0; x < 6; ++x) {
                    const bool inside = x >= bounds.x && x < bounds.x + bounds.width &&
                                        y >= bounds.y && y < bounds.y + bounds.height;
                    const uint16_t expected = inside ? 0x07E0 : transparent ? 0x001F : 0xF800;
                    assert(draw_buffer[y * 6 + x] == expected);
                }
            }
        }
    }
    lv_display_delete(display);
}

static void check_thin_image() {
    std::vector<uint8_t> source(100 * 4, 255);
    uint8_t* png = nullptr;
    size_t png_size = 0;
    assert(lodepng_encode32(&png, &png_size, source.data(), 100, 1) == 0);
    uint16_t* output = nullptr;
    size_t output_size = 0;
    ImageContentBounds bounds = {};
    assert(image_decode_to_rgb565(png, png_size, 2, 2, IMAGE_SCALE_LETTERBOX,
                                  &output, &output_size, 0, &bounds));
    assert(bounds.x == 0 && bounds.y == 0 && bounds.width == 2 && bounds.height == 1);
    assert(output[0] == 0xFFFF && output[1] == 0xFFFF && output[2] == 0);
    free(output);
    assert(!image_decode_to_rgb565(nullptr, 0, 2, 2, IMAGE_SCALE_LETTERBOX,
                                   &output, &output_size, 0, &bounds));
    assert(!output && output_size == 0 && bounds.width == 0 && bounds.height == 0);
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
    check_transparent_letterbox();
    check_thin_image();
    puts("image_decoder: PASS");
}

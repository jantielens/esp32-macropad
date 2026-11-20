#pragma once

#include "../image_decoder.h"
#include <lvgl.h>

inline void pad_set_image_frame(lv_obj_t* image, lv_image_dsc_t& descriptor,
                               const uint16_t* pixels, uint16_t width, uint16_t height,
                               const ImageContentBounds& content, bool transparent) {
    const ImageContentBounds visible = transparent ? content : ImageContentBounds{0, 0, width, height};
    descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    descriptor.header.flags = 0;
    descriptor.header.w = width;
    descriptor.header.h = height;
    descriptor.header.stride = width * 2;
    descriptor.data_size = (uint32_t)width * height * 2;
    descriptor.data = (const uint8_t*)pixels;
    lv_obj_set_size(image, visible.width, visible.height);
    // Preserve the original placement when centered sizes round differently.
    lv_obj_align(image, LV_ALIGN_CENTER,
                 (int32_t)visible.x + visible.width / 2 - width / 2,
                 (int32_t)visible.y + visible.height / 2 - height / 2);
    // Clip the full frame to its content without shifting the buffer pointer.
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_image_set_offset_x(image, -(int32_t)visible.x);
    lv_image_set_offset_y(image, -(int32_t)visible.y);
    lv_image_set_src(image, &descriptor);
    lv_obj_invalidate(image);
}

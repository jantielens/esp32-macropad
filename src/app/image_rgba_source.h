#pragma once

#include <stddef.h>
#include <stdint.h>

static inline bool image_rgba_source_is_valid(
    uint32_t width, uint32_t height, size_t stride, size_t data_size)
{
    if (width == 0 || height == 0 || width > SIZE_MAX / 4) return false;

    size_t row_bytes = (size_t)width * 4;
    if (stride < row_bytes || height > SIZE_MAX / stride) return false;

    return stride * height <= data_size;
}

static inline const uint8_t* image_rgba_source_pixel(
    const uint8_t* src, size_t row, size_t stride, size_t column)
{
    return src + row * stride + column * 4;
}
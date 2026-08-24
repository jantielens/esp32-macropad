#pragma once

#include <stddef.h>
#include <stdint.h>

// Encodes a tightly packed grayscale image. The caller owns the returned JPEG.
bool jpeg_encode_gray8(const uint8_t* pixels, uint16_t width, uint16_t height,
                       uint8_t quality, uint8_t** jpeg_data, size_t* jpeg_size);

// Encodes a tightly packed RGB565 image. The caller owns the returned JPEG.
bool jpeg_encode_rgb565(const uint8_t* pixels, uint16_t width, uint16_t height,
                        uint8_t quality, uint8_t** jpeg_data, size_t* jpeg_size);
#pragma once

#include "board_config.h"

#if HAS_IMAGE_FETCH

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================================
// Image Decoder — stateless JPEG/PNG decode + cover-mode scale to RGB565
// ============================================================================
// Decodes JPEG (via LVGL's built-in tjpgd) or PNG (via lodepng) to an RGB565
// pixel buffer sized exactly target_w × target_h.  Format is auto-detected
// from magic bytes.  Cover-mode scaling fills the target rect and center-crops
// any excess (like CSS object-fit: cover).
//
// All intermediate buffers are allocated from PSRAM.  The returned output
// buffer is also PSRAM-allocated; caller must free with heap_caps_free().
//
// This module has no FreeRTOS or LVGL runtime dependency — it can be called
// from any task context.  Designed for reuse by the image-fetch manager and
// a future webcam screen.

enum ImageFormat : uint8_t {
    IMAGE_FORMAT_UNKNOWN = 0,
    IMAGE_FORMAT_JPEG    = 1,
    IMAGE_FORMAT_PNG     = 2,
};

// Detect image format from the first bytes of data.
ImageFormat image_detect_format(const uint8_t* data, size_t len);

// Decode image data to an RGB565 pixel buffer, scaled to cover the target
// dimensions (fill + center-crop, bilinear interpolation).
//
//   data / len       — raw JPEG or PNG bytes
//   target_w/h       — desired output pixel dimensions
//   out_pixels       — receives heap_caps_malloc'd RGB565 buffer (PSRAM)
//   out_size         — receives byte count of the output buffer
//
// Returns true on success.  On failure, *out_pixels is NULL.
bool image_decode_to_rgb565(
    const uint8_t* data, size_t len,
    uint16_t target_w, uint16_t target_h,
    uint16_t** out_pixels, size_t* out_size);

#endif // HAS_IMAGE_FETCH

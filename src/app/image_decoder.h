#pragma once

#include "board_config.h"

#if HAS_IMAGE_FETCH

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ============================================================================
// Image Decoder — stateless JPEG/PNG decode + scaled to RGB565
// ============================================================================
// Decodes JPEG (via LVGL's built-in tjpgd) or PNG (via lodepng) to an RGB565
// pixel buffer sized exactly target_w × target_h.  Format is auto-detected
// from magic bytes.  Two scale modes:
//   - Cover (default): fill target rect, center-crop excess (CSS object-fit: cover)
//   - Letterbox: fit inside target rect, black bars (CSS object-fit: contain)
//
// All intermediate buffers are allocated from PSRAM.  The returned output
// buffer is also PSRAM-allocated; caller must free with heap_caps_free().
//
// This module has no FreeRTOS or LVGL runtime dependency — it can be called
// from any task context.  Designed for reuse by the image-fetch manager and
// webcam-style button backgrounds.

enum ImageFormat : uint8_t {
    IMAGE_FORMAT_UNKNOWN = 0,
    IMAGE_FORMAT_JPEG    = 1,
    IMAGE_FORMAT_PNG     = 2,
};

enum ImageScaleMode : uint8_t {
    IMAGE_SCALE_COVER    = 0,   // Fill target, center-crop excess (CSS object-fit: cover)
    IMAGE_SCALE_LETTERBOX = 1,  // Fit inside target, black bars (CSS object-fit: contain)
};

// Detect image format from the first bytes of data.
ImageFormat image_detect_format(const uint8_t* data, size_t len);

// Decode image data to an RGB565 pixel buffer, scaled to fit the target
// dimensions using the specified scale mode (bilinear interpolation).
//
//   data / len       — raw JPEG or PNG bytes
//   target_w/h       — desired output pixel dimensions
//   scale_mode       — IMAGE_SCALE_COVER (fill+crop) or IMAGE_SCALE_LETTERBOX (fit+bars)
//   out_pixels       — receives heap_caps_malloc'd RGB565 buffer (PSRAM)
//   out_size         — receives byte count of the output buffer
//
// Returns true on success.  On failure, *out_pixels is NULL.
bool image_decode_to_rgb565(
    const uint8_t* data, size_t len,
    uint16_t target_w, uint16_t target_h,
    ImageScaleMode scale_mode,
    uint16_t** out_pixels, size_t* out_size);

#endif // HAS_IMAGE_FETCH

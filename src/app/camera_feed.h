#pragma once

#include "camera.h"

#include <stdint.h>

enum CameraFeedOutput : uint8_t {
    CAMERA_FEED_OUTPUT_RGB565 = 1 << 0,
    CAMERA_FEED_OUTPUT_JPEG = 1 << 1,
};

struct CameraFeedFrame {
    const CameraJpegFrame* jpeg;
    const CameraRgb565Frame* rgb565;
    uint32_t generation;
    uint8_t slot;
};

struct CameraFeedState {
    uint16_t width;
    uint16_t height;
    uint8_t jpeg_quality;
    uint16_t active_consumers;
    uint16_t rgb565_consumers;
    uint16_t jpeg_consumers;
    uint32_t interval_ms;
    uint32_t generation;
    CameraCaptureTiming timing;
};

// Allocates the shared cache. Call after camera_init().
void camera_feed_init();
void camera_feed_deinit();

// Keeps the producer active while at least one consumer has acquired demand.
void camera_feed_acquire_demand(CameraFeedOutput output);
void camera_feed_release_demand(CameraFeedOutput output);

// Returns the current producer configuration and latest published generation.
CameraFeedState camera_feed_get_state();

// Borrows the newest complete frame until camera_feed_release_frame().
bool camera_feed_acquire_frame(CameraFeedFrame* frame, CameraFeedOutput output);
void camera_feed_release_frame(const CameraFeedFrame* frame);

// Captures at most one new frame per second. Must run on the Arduino loop.
void camera_feed_loop();
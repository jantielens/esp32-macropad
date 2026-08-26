#pragma once

#include "board_config.h"

#if HAS_CAMERA

#include <stdint.h>

#define CAMERA_MOTION_FPS_MIN 1
#define CAMERA_MOTION_FPS_MAX 2
#define CAMERA_MOTION_FPS_DEFAULT 1
#define CAMERA_MOTION_SENSITIVITY_MIN 1
#define CAMERA_MOTION_SENSITIVITY_MAX 10
#define CAMERA_MOTION_SENSITIVITY_DEFAULT 5
#define CAMERA_PRESENCE_HOLD_SECONDS_MIN 10
#define CAMERA_PRESENCE_HOLD_SECONDS_MAX 600
#define CAMERA_PRESENCE_HOLD_SECONDS_DEFAULT 60

struct CameraMotionSettings {
    bool enabled;
    uint8_t sample_fps;
    uint8_t sensitivity;
    uint16_t presence_hold_seconds;
};

struct CameraMotionStatus {
    bool enabled;
    bool presence;
    bool has_last_motion;
    uint32_t last_motion_epoch;
    uint16_t changed_tiles;
    uint32_t score;
};

CameraMotionSettings camera_motion_get_settings();
bool camera_motion_set_settings(const CameraMotionSettings& settings);
CameraMotionStatus camera_motion_get_status();
bool camera_motion_is_enabled();
void camera_motion_deinit();

// Captures and evaluates RAW10 frames at the configured cadence. Must run on
// the Arduino main loop. It performs no RGB565 conversion or JPEG encoding.
void camera_motion_loop();

// Returns each motion-derived presence state transition exactly once. Must run
// on the Arduino main loop.
bool camera_motion_take_presence_change(bool* presence);

#endif // HAS_CAMERA
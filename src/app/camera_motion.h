#pragma once

#include "board_config.h"

#if HAS_CAMERA

#include "camera.h"

#include <stdint.h>

#define CAMERA_MOTION_ANALYZE_EVERY_MIN 1
#define CAMERA_MOTION_ANALYZE_EVERY_MAX 4
#define CAMERA_MOTION_ANALYZE_EVERY_DEFAULT 2
#define CAMERA_MOTION_SENSITIVITY_MIN 1
#define CAMERA_MOTION_SENSITIVITY_MAX 10
#define CAMERA_MOTION_SENSITIVITY_DEFAULT 5
#define CAMERA_PRESENCE_HOLD_SECONDS_MIN 10
#define CAMERA_PRESENCE_HOLD_SECONDS_MAX 600
#define CAMERA_PRESENCE_HOLD_SECONDS_DEFAULT 60

struct CameraMotionThresholds {
    uint16_t changed_tiles;
    uint32_t score;
};

struct CameraMotionSettings {
    bool enabled;
    uint8_t analyze_every_nth_frame;
    uint8_t sensitivity;
    uint16_t presence_hold_seconds;
#if HAS_DISPLAY
    bool keep_display_awake;
#endif
};

struct CameraMotionStatus {
    bool enabled;
    bool presence;
    bool has_last_motion;
    uint32_t last_motion_epoch;
    uint32_t last_motion_ms;
    bool has_last_sample;
    uint32_t last_sample_ms;
    bool baseline_ready;
    bool global_change;
    uint8_t confirm_frames;
    uint16_t changed_tiles;
    uint32_t score;
};

CameraMotionSettings camera_motion_get_settings();
CameraMotionThresholds camera_motion_get_thresholds(uint8_t sensitivity);
bool camera_motion_set_settings(const CameraMotionSettings& settings);
CameraMotionStatus camera_motion_get_status();
bool camera_motion_is_enabled();
void camera_motion_deinit();

// Runs presence timeout handling. Must run on the Arduino main loop.
void camera_motion_loop();

// Evaluates every configured Nth shared RAW10 frame. Must run on the Arduino
// main loop after camera capture and before the frame is released or reused.
void camera_motion_on_raw_frame(const CameraRawFrame& raw, bool analyze_every_frame = false);

// Returns each motion-derived presence state transition exactly once. Must run
// on the Arduino main loop.
bool camera_motion_take_presence_change(bool* presence);

// Returns each confirmed motion sample at most once. Must run on the Arduino
// main loop. Repeated samples are coalesced until consumed.
bool camera_motion_take_confirmed_motion();

#endif // HAS_CAMERA
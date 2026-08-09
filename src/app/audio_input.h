#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board_config.h"

struct AudioInputMeterSnapshot {
    uint8_t rms;
    uint8_t peak;
    bool active;
};

#if HAS_AUDIO_INPUT

#include "audio_input_driver.h"

bool audio_input_available();

// Reserve the microphone for the calling task. Only one task may own a capture
// session at a time; the same owner can call this again without changing state.
bool audio_input_start_capture();

// Read up to frame_count native, interleaved PCM frames for the calling capture
// owner. Returns the number of frames read, or zero on timeout/error/not-owner.
size_t audio_input_read_frames(int16_t* frames, size_t frame_count, uint32_t timeout_ms);

// Release the calling task's capture session. Calls from another task are ignored.
void audio_input_stop_capture();

// Return the board-native PCM format. Returns an all-zero format when unavailable.
AudioInputFormat audio_input_format();

// Demand-driven microphone level meter for display bindings. Each binding
// resolution refreshes a brief sampling lease; the task sleeps otherwise.
void audio_input_meter_init();
void audio_input_meter_request();
// Copy a recently sampled level snapshot. Returns false until a fresh sample
// is available, so callers can use their normal binding fallback.
bool audio_input_meter_get_snapshot(AudioInputMeterSnapshot* out);

#else

struct AudioInputFormat {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
};

inline bool audio_input_available() { return false; }
inline bool audio_input_start_capture() { return false; }
inline size_t audio_input_read_frames(int16_t*, size_t, uint32_t) { return 0; }
inline void audio_input_stop_capture() {}
inline AudioInputFormat audio_input_format() { return {}; }
inline void audio_input_meter_init() {}
inline void audio_input_meter_request() {}
inline bool audio_input_meter_get_snapshot(AudioInputMeterSnapshot* out) {
    if (out) *out = {};
    return false;
}

#endif // HAS_AUDIO_INPUT
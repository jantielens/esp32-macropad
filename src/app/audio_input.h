#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board_config.h"

struct AudioInputMeterSnapshot {
    uint8_t rms;
    uint8_t peak;
    bool active;
};

struct AudioInputDiagnostics {
    uint16_t rms;
    uint16_t peak;
    bool fresh;
    bool sampled;
    bool in_use;
    bool available;
    bool ota_active;
    bool input_full_scale;
    bool gain_clipped;
};

#if HAS_AUDIO_INPUT

#include "audio_input_driver.h"
#include "audio_input_processing.h"

bool audio_input_available();
bool audio_input_capture_owned();

// Reserve the microphone for the calling task. Only one task may own a capture
// session at a time; the same owner can call this again without changing state.
bool audio_input_start_capture();

// Read board-native interleaved PCM with shared input processing. A timeout may include partial frames.
AudioInputReadResult audio_input_read_frames(int16_t* frames, size_t frame_count,
                                              uint32_t timeout_ms);
AudioInputReadStatus audio_input_capture_status();

// Publish settings for adoption by the capture owner at the next block.
bool audio_input_set_processing(AudioInputProcessingSettings settings);
AudioInputProcessingSettings audio_input_get_processing();

// Stop sampling without releasing the calling task's reservation.
bool audio_input_pause_capture();

// Release the calling task's capture session only after hardware stops.
bool audio_input_stop_capture();

// Return the board-native PCM format. Returns an all-zero format when unavailable.
AudioInputFormat audio_input_format();

// Demand-driven microphone level meter for display bindings. Each binding
// resolution refreshes a brief sampling lease; the task sleeps otherwise.
void audio_input_meter_init();
void audio_input_meter_request();
// Copy a recently sampled level snapshot. Returns false until a fresh sample
// is available, so callers can use their normal binding fallback.
bool audio_input_meter_get_snapshot(AudioInputMeterSnapshot* out);
// Renew the meter lease separately before reading this non-blocking snapshot.
AudioInputDiagnostics audio_input_diagnostics();

#else

struct AudioInputFormat {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
};

enum class AudioInputReadStatus { Ok, Timeout, Error, Overrun };
struct AudioInputReadResult {
    size_t frames;
    AudioInputReadStatus status;
};

inline bool audio_input_available() { return false; }
inline bool audio_input_capture_owned() { return false; }
inline bool audio_input_start_capture() { return false; }
inline AudioInputReadResult audio_input_read_frames(int16_t*, size_t, uint32_t) {
    return {0, AudioInputReadStatus::Error};
}
inline AudioInputReadStatus audio_input_capture_status() { return AudioInputReadStatus::Error; }
inline bool audio_input_pause_capture() { return false; }
inline bool audio_input_stop_capture() { return false; }
inline AudioInputFormat audio_input_format() { return {}; }
inline void audio_input_meter_init() {}
inline void audio_input_meter_request() {}
inline bool audio_input_meter_get_snapshot(AudioInputMeterSnapshot* out) {
    if (out) *out = {};
    return false;
}

#endif // HAS_AUDIO_INPUT
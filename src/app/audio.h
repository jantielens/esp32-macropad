#pragma once

#include "board_config.h"
#include <stdint.h>

#if HAS_AUDIO

// Initialize the ES8311 codec, I2S TX channel, and background audio task.
// Must be called after Wire.begin() (e.g. after touch_manager_init on boards
// that share the I2C bus).
// initial_volume: 0-100 (from NVS config)
void audio_init(uint8_t initial_volume);

// Set device-level volume (0-100). Takes effect immediately.
void audio_set_volume(uint8_t vol_0_100);

// Get current device-level volume (0-100).
uint8_t audio_get_volume();

// Queue a beep pattern for async playback. Non-blocking.
// pattern: space-delimited steps. "freq:dur" = tone, bare "dur" = silence gap (ms).
//          e.g. "1000:200" or "1000:200 100 1000:200"
//          NULL or "" plays default 1000Hz 200ms beep.
// volume_override: 1-100 = use this volume for this beep only (restores after).
//                  0 = use current device volume.
void audio_beep(const char* pattern, uint8_t volume_override);

#endif // HAS_AUDIO

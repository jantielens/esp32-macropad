#pragma once

#include "board_config.h"
#include <stddef.h>
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

// Start looping a beep pattern until audio_stop() is called.
// The pattern should include a trailing silence gap to control repeat cadence.
// volume_override: same semantics as audio_beep().
void audio_play_loop(const char* pattern, uint8_t volume_override);

// Stop any currently playing or looping audio. Flushes the queue.
void audio_stop();

// Returns true if a pattern is currently playing (one-shot or loop).
bool audio_is_playing();

// ---------------------------------------------------------------------------
// Internal: output-starvation instrumentation.
// Shared by the tone path (audio.cpp) and the MP3 path (sound_player.cpp).
// Not part of the public audio API; do not call from application code.
// ---------------------------------------------------------------------------
class AudioOutputDriver;

struct AudioStarvationStats {
	int64_t previous_write_complete_us;  // 0 = no write yet this clip
	int64_t worst_gap_us;
	uint32_t event_count;
};

// Writes one PCM block through `driver`, measuring the producer-side gap since
// the previous write returned. Returns the driver's write result unchanged.
// Zero-initialise at clip start: `AudioStarvationStats stats = {};`
bool audio_write_with_stats(AudioOutputDriver* driver, const int16_t* frames,
							size_t frame_count, AudioStarvationStats* stats);

// Emits the end-of-clip line: event count, worst gap, buffered duration.
void audio_log_starvation(const AudioStarvationStats& stats);

#if HAS_SOUND_PLAYER
// Play an MP3 sound file from LittleFS. Non-blocking (queued).
// filename: sound name (without path or extension), e.g. "doorbell"
// volume_override: 1-100 = use this volume, 0 = use device volume.
void audio_play_sound(const char* filename, uint8_t volume_override);
#endif

#endif // HAS_AUDIO

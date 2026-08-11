#pragma once

#include "board_config.h"
#include <stddef.h>
#include <stdint.h>

#if HAS_AUDIO

// Initialize the board-selected audio output driver, I2S TX channel, and background audio task.
// I2C-attached codec boards must call this after Wire.begin() (e.g. after
// touch_manager_init on boards that share the I2C bus).
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
#include "music_catalog.h"
#include "music_catalog_store.h"
#include "music_command.h"

// Play an MP3 sound file from LittleFS. Non-blocking (queued).
// filename: sound name (without path or extension), e.g. "doorbell"
// volume_override: 1-100 = use this volume, 0 = use device volume.
void audio_play_sound(const char* filename, uint8_t volume_override);

// Guard invoked on the audio task immediately before a memory-backed MP3 starts.
// The buffer ownership transfers on every call and is released by the audio task.
typedef bool (*AudioPlaybackGuard)(uint32_t generation);
void audio_play_mp3_buffer(uint8_t* mp3, size_t mp3_size, uint8_t volume_override,
                           AudioPlaybackGuard guard, uint32_t generation);

enum AudioMusicStatus : uint8_t {
    AUDIO_MUSIC_STOPPED,
    AUDIO_MUSIC_PLAYING,
    AUDIO_MUSIC_PAUSED,
    AUDIO_MUSIC_EMPTY,
    AUDIO_MUSIC_UNAVAILABLE,
    AUDIO_MUSIC_ERROR,
};

struct AudioMusicInfo {
    AudioMusicStatus status;
    uint8_t index;
    uint8_t count;
    uint64_t total_us;
    uint64_t elapsed_us;
    char file[192];
    Mp3Metadata metadata;
};

enum AudioMusicSubmitResult : uint8_t {
    AUDIO_MUSIC_SUBMIT_QUEUED,
    AUDIO_MUSIC_SUBMIT_BUSY,
    AUDIO_MUSIC_SUBMIT_UNAVAILABLE,
    AUDIO_MUSIC_SUBMIT_INVALID,
};

// Submit a bounded, non-blocking Music transport request to the audio worker.
AudioMusicSubmitResult audio_music_command(MusicCommand command);

// Snapshot read-only Music state for bindings and management views.
void audio_get_music_info(AudioMusicInfo* out);

// Snapshot the Music catalog discovered by the audio worker.
bool audio_get_music_catalog_snapshot(MusicCatalogSnapshot* out);
bool audio_get_music_catalog_status(MusicCatalogStatus* out);

// Read small catalog properties without copying its fixed-size path array.
bool audio_get_music_catalog_count(uint8_t* out_count);

// Rebuild the published Music catalog on the audio worker. Returns false when
// the worker is busy or does not respond before timeout_ms.
bool audio_music_refresh_catalog(uint32_t timeout_ms);

// Reserve Music storage while no Music playback or MP3 alert is active.
// The caller must release a successful reservation with
// audio_music_storage_mutation_end().
bool audio_music_storage_mutation_begin();
void audio_music_storage_mutation_end(bool catalog_changed);

#endif // HAS_SOUND_PLAYER

#endif // HAS_AUDIO

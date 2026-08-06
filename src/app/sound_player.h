#pragma once

#include "board_config.h"

#if HAS_SOUND_PLAYER

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
class AudioOutputDriver;
#endif

enum SoundPlayerStepResult : uint8_t {
    SOUND_PLAYER_STEP_PLAYING,
    SOUND_PLAYER_STEP_COMPLETE,
    SOUND_PLAYER_STEP_ERROR,
};

struct SoundPlayer;

typedef void (*SoundPlayerPcmTransform)(void* context, int16_t* frames,
                                        size_t frame_count);

// Open an MP3 at an already canonical absolute storage path. The returned
// session is owned by the audio worker and must be closed exactly once.
SoundPlayer* sound_player_begin_path(AudioOutputDriver* output_driver,
                                     const char* path,
                                     SoundPlayerPcmTransform transform = nullptr,
                                     void* transform_context = nullptr);

// Verify that an MP3 at a canonical storage path scans cleanly to EOF with at
// least one decodable frame, without loading the complete file into memory.
bool sound_player_validate_path(const char* path);

// Decode and emit at most one MP3 frame through the supplied session.
SoundPlayerStepResult sound_player_step(SoundPlayer* player);

// Snapshot current-track timing. Returns false when duration pre-scan could
// not determine a duration; elapsed_us remains zero until output is accepted.
bool sound_player_get_timing(const SoundPlayer* player, uint64_t* total_us,
                             uint64_t* elapsed_us);

// Release the decoder, buffers, and open file associated with a session.
void sound_player_close(SoundPlayer* player);

// Play an MP3 sound file from LittleFS.
// Called from the audio task context — blocks until playback completes or
// stop_flag is set.
// output_driver: non-owning audio output driver for PCM samples
// filename: sound name (without path/extension)
// stop_flag: pointer to volatile bool checked between frames for early abort
// Returns true on success.
bool sound_player_play(AudioOutputDriver* output_driver, const char* filename,
                       volatile bool* stop_flag);

#ifdef __cplusplus
#endif

#endif // HAS_SOUND_PLAYER

#pragma once

#include "board_config.h"

#if HAS_SOUND_PLAYER

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
class AudioOutputDriver;
extern "C" {
#endif

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
}
#endif

#endif // HAS_SOUND_PLAYER

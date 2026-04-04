#pragma once

#include "board_config.h"

#if HAS_SOUND_PLAYER

#include <stdbool.h>
#include <stdint.h>
#include "driver/i2s_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Play an MP3 sound file from LittleFS.
// Called from the audio task context — blocks until playback completes or
// stop_flag is set.
// tx_handle: I2S TX channel handle for writing PCM samples
// filename: sound name (without path/extension)
// stop_flag: pointer to volatile bool checked between frames for early abort
// Returns true on success.
bool sound_player_play(i2s_chan_handle_t tx_handle, const char* filename,
                       volatile bool* stop_flag);

#ifdef __cplusplus
}
#endif

#endif // HAS_SOUND_PLAYER

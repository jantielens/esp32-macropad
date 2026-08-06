#pragma once

#include <stdint.h>

enum MusicCommand : uint8_t {
    MUSIC_COMMAND_PLAY_PAUSE,
    MUSIC_COMMAND_NEXT,
    MUSIC_COMMAND_PREVIOUS,
    MUSIC_COMMAND_STOP,
};

bool music_command_parse(const char* value, MusicCommand* out);
const char* music_command_name(MusicCommand command);
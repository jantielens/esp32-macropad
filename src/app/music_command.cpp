#include "music_command.h"

#include <string.h>

bool music_command_parse(const char* value, MusicCommand* out) {
    if (!value || !out) return false;
    if (strcmp(value, "play_pause") == 0) *out = MUSIC_COMMAND_PLAY_PAUSE;
    else if (strcmp(value, "next") == 0) *out = MUSIC_COMMAND_NEXT;
    else if (strcmp(value, "previous") == 0) *out = MUSIC_COMMAND_PREVIOUS;
    else if (strcmp(value, "stop") == 0) *out = MUSIC_COMMAND_STOP;
    else return false;
    return true;
}

const char* music_command_name(MusicCommand command) {
    switch (command) {
        case MUSIC_COMMAND_PLAY_PAUSE: return "play_pause";
        case MUSIC_COMMAND_NEXT: return "next";
        case MUSIC_COMMAND_PREVIOUS: return "previous";
        case MUSIC_COMMAND_STOP: return "stop";
    }
    return "";
}
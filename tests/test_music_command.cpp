#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "music_command.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    const struct {
        const char* text;
        MusicCommand command;
    } valid[] = {
        {"play_pause", MUSIC_COMMAND_PLAY_PAUSE},
        {"next", MUSIC_COMMAND_NEXT},
        {"previous", MUSIC_COMMAND_PREVIOUS},
        {"stop", MUSIC_COMMAND_STOP},
    };
    for (const auto& entry : valid) {
        MusicCommand parsed = MUSIC_COMMAND_STOP;
        check(music_command_parse(entry.text, &parsed) && parsed == entry.command,
              "valid Music command must parse");
        check(std::strcmp(music_command_name(parsed), entry.text) == 0,
              "Music command must serialize canonically");
    }
    MusicCommand ignored = MUSIC_COMMAND_STOP;
    check(!music_command_parse("skip", &ignored), "unknown Music command must fail");
    check(!music_command_parse(nullptr, &ignored), "null Music command must fail");
    std::puts("music command checks passed");
    return 0;
}
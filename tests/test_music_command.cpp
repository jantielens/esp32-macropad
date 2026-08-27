#include <cstring>
#include <gtest/gtest.h>

#include "music_command.h"

TEST(MusicCommand, ParsesAndSerializesKnownCommands) {
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
        ASSERT_TRUE(music_command_parse(entry.text, &parsed));
        EXPECT_EQ(parsed, entry.command);
        EXPECT_STREQ(music_command_name(parsed), entry.text);
    }
    MusicCommand ignored = MUSIC_COMMAND_STOP;
    EXPECT_FALSE(music_command_parse("skip", &ignored));
    EXPECT_FALSE(music_command_parse(nullptr, &ignored));
}
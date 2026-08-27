#include <cstring>
#include <gtest/gtest.h>

#include "device_classes/voice_assistant/voice_wav.h"

static uint16_t read_u16_le(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32_le(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

TEST(VoiceWav, BuildsPcmHeader) {
    uint8_t header[VOICE_WAV_HEADER_BYTES] = {};
    voice_wav_build_header(header, 960000, 16000, 1, 16);

    EXPECT_EQ(std::memcmp(header, "RIFF", 4), 0);
    EXPECT_EQ(read_u32_le(header + 4), 960036u);
    EXPECT_EQ(std::memcmp(header + 8, "WAVEfmt ", 8), 0);
    EXPECT_EQ(read_u16_le(header + 20), 1);
    EXPECT_EQ(read_u16_le(header + 22), 1);
    EXPECT_EQ(read_u32_le(header + 24), 16000u);
    EXPECT_EQ(read_u32_le(header + 28), 32000u);
    EXPECT_EQ(read_u16_le(header + 32), 2);
    EXPECT_EQ(read_u16_le(header + 34), 16);
    EXPECT_EQ(std::memcmp(header + 36, "data", 4), 0);
    EXPECT_EQ(read_u32_le(header + 40), 960000u);
}
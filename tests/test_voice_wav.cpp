#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "device_classes/voice_assistant/voice_wav.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static uint16_t read_u16_le(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32_le(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

int main() {
    uint8_t header[VOICE_WAV_HEADER_BYTES] = {};
    voice_wav_build_header(header, 960000, 16000, 1, 16);

    check(std::memcmp(header, "RIFF", 4) == 0, "RIFF signature");
    check(read_u32_le(header + 4) == 960036, "RIFF payload size");
    check(std::memcmp(header + 8, "WAVEfmt ", 8) == 0, "WAV format identifiers");
    check(read_u16_le(header + 20) == 1, "PCM format code");
    check(read_u16_le(header + 22) == 1, "mono channel count");
    check(read_u32_le(header + 24) == 16000, "sample rate");
    check(read_u32_le(header + 28) == 32000, "byte rate");
    check(read_u16_le(header + 32) == 2, "block alignment");
    check(read_u16_le(header + 34) == 16, "sample width");
    check(std::memcmp(header + 36, "data", 4) == 0, "data identifier");
    check(read_u32_le(header + 40) == 960000, "data payload size");
    std::puts("Voice WAV checks passed");
    return 0;
}
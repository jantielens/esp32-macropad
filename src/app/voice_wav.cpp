#include "board_config.h"
#if IS_VOICE_ASSISTANT || HAS_VOICE_CLIPS

#include "voice_wav.h"
#include <string.h>

namespace {
void write_u16_le(uint8_t* out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

void write_u32_le(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

#if HAS_VOICE_CLIPS
uint16_t read_u16_le(const uint8_t* data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

uint32_t read_u32_le(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}
#endif
} // namespace

void voice_wav_build_header(uint8_t* out, uint32_t data_bytes, uint32_t sample_rate,
                            uint16_t channels, uint16_t bits_per_sample) {
    const uint16_t block_align = channels * (bits_per_sample / 8);
    const uint32_t byte_rate = sample_rate * block_align;
    memcpy(out, "RIFF", 4);
    write_u32_le(out + 4, data_bytes + 36);
    memcpy(out + 8, "WAVEfmt ", 8);
    write_u32_le(out + 16, 16);
    write_u16_le(out + 20, 1);
    write_u16_le(out + 22, channels);
    write_u32_le(out + 24, sample_rate);
    write_u32_le(out + 28, byte_rate);
    write_u16_le(out + 32, block_align);
    write_u16_le(out + 34, bits_per_sample);
    memcpy(out + 36, "data", 4);
    write_u32_le(out + 40, data_bytes);
}

#if HAS_VOICE_CLIPS
bool voice_wav_read_header(const uint8_t* header, size_t header_size, uint64_t file_size,
                           VoiceWavInfo* info) {
    if (!header || !info || header_size < VOICE_WAV_HEADER_BYTES ||
        memcmp(header, "RIFF", 4) != 0 ||
        memcmp(header + 8, "WAVEfmt ", 8) != 0 ||
        read_u32_le(header + 16) != 16 || read_u16_le(header + 20) != 1 ||
        read_u16_le(header + 34) != 16 || memcmp(header + 36, "data", 4) != 0) {
        return false;
    }

    const uint16_t channels = read_u16_le(header + 22);
    const uint16_t block_align = read_u16_le(header + 32);
    const uint32_t sample_rate = read_u32_le(header + 24);
    const uint32_t data_bytes = read_u32_le(header + 40);
    const uint64_t byte_rate = (uint64_t)sample_rate * block_align;
    if (channels == 0 || (uint32_t)channels * 2 != block_align || sample_rate == 0 ||
        byte_rate > UINT32_MAX || read_u32_le(header + 28) != byte_rate ||
        data_bytes == 0 || data_bytes % block_align != 0 ||
        (uint64_t)data_bytes + 36 > UINT32_MAX ||
        read_u32_le(header + 4) != data_bytes + 36 ||
        file_size != (uint64_t)VOICE_WAV_HEADER_BYTES + data_bytes) {
        return false;
    }

    *info = {data_bytes, sample_rate, channels, block_align};
    return true;
}
#endif

#endif // IS_VOICE_ASSISTANT || HAS_VOICE_CLIPS
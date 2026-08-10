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
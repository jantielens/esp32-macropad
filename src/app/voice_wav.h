#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t VOICE_WAV_HEADER_BYTES = 44;

struct VoiceWavInfo {
    uint32_t data_bytes;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t block_align;
};

void voice_wav_build_header(uint8_t* out, uint32_t data_bytes, uint32_t sample_rate,
                            uint16_t channels, uint16_t bits_per_sample);

// Validate a finalized canonical 16-bit PCM WAV header against its file size.
bool voice_wav_read_header(const uint8_t* header, size_t header_size, uint64_t file_size,
                           VoiceWavInfo* info);
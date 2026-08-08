#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t STT_WAV_HEADER_BYTES = 44;

void stt_wav_build_header(uint8_t* out, uint32_t data_bytes, uint32_t sample_rate,
                          uint16_t channels, uint16_t bits_per_sample);
#pragma once

#include <stddef.h>
#include <stdint.h>

struct AudioPcmLevels {
    uint8_t rms;
    uint8_t peak;
};

inline uint8_t audio_pcm_level(uint32_t amplitude) {
    if (amplitude > 32767U) amplitude = 32767U;
    return (uint8_t)((amplitude * 100U + 16383U) / 32767U);
}

inline uint32_t audio_pcm_isqrt_u64(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = 1ULL << 62;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

inline AudioPcmLevels audio_pcm_levels(const int16_t* samples, size_t frame_count,
                                       uint8_t channels) {
    AudioPcmLevels levels = {};
    if (!samples || frame_count == 0 || channels == 0) return levels;
    uint64_t sum_sq = 0;
    uint32_t peak = 0;
    for (size_t frame = 0; frame < frame_count; ++frame) {
        int32_t mono = 0;
        for (uint8_t channel = 0; channel < channels; ++channel) {
            mono += samples[frame * channels + channel];
        }
        mono /= channels;
        sum_sq += (uint64_t)(mono * (int64_t)mono);
        const uint32_t amplitude = mono < 0 ? (uint32_t)-mono : (uint32_t)mono;
        if (amplitude > peak) peak = amplitude;
    }
    levels.rms = audio_pcm_level(audio_pcm_isqrt_u64(sum_sq / frame_count));
    levels.peak = audio_pcm_level(peak);
    return levels;
}

inline uint8_t audio_pcm_rms_level(const int16_t* samples, size_t frame_count, uint8_t channels) {
    return audio_pcm_levels(samples, frame_count, channels).rms;
}

inline uint8_t audio_pcm_peak_level(const int16_t* samples, size_t frame_count, uint8_t channels) {
    return audio_pcm_levels(samples, frame_count, channels).peak;
}
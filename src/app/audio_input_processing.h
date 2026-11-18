#pragma once

#include "board_config.h"

#if HAS_AUDIO_INPUT

#include <math.h>
#include <stddef.h>
#include <stdint.h>

struct AudioInputProcessingSettings {
    int8_t gain_db;
    bool reduce_rumble;
};

struct AudioInputBlockDiagnostics {
    uint64_t sum_squares;
    uint16_t peak;
    bool input_full_scale;
    bool gain_clipped;
};

class AudioInputProcessing {
public:
    void configure(AudioInputProcessingSettings settings, uint32_t sample_rate, uint8_t channels) {
        if (settings.reduce_rumble != settings_.reduce_rumble ||
            sample_rate != sample_rate_ || channels != channels_) {
            reset();
        }
        settings_ = settings;
        sample_rate_ = sample_rate;
        channels_ = channels;
        gain_ = powf(10.0f, settings.gain_db / 20.0f);
        const float rc_rate = 502.6548246f / sample_rate;
        alpha_ = 1.0f / (1.0f + rc_rate);
    }

    void reset() {
        for (unsigned i = 0; i < 2; ++i) {
            previous_input_[i] = 0;
            previous_output_[i] = 0;
        }
    }

    void process(int16_t* samples, size_t frames, AudioInputBlockDiagnostics* diagnostics = nullptr) {
        if (!settings_.reduce_rumble && settings_.gain_db == 0 && !diagnostics) return;
        for (size_t frame = 0; frame < frames; ++frame) {
            for (uint8_t channel = 0; channel < channels_; ++channel) {
                const size_t index = frame * channels_ + channel;
                const float input = samples[index];
                if (diagnostics && (samples[index] == -32768 || samples[index] == 32767))
                    diagnostics->input_full_scale = true;
                float output = input;
                if (settings_.reduce_rumble) {
                    output = alpha_ * (previous_output_[channel] + input - previous_input_[channel]);
                    previous_input_[channel] = input;
                    previous_output_[channel] = output;
                }
                output *= gain_;
                if (diagnostics && (output > 32767.0f || output < -32768.0f))
                    diagnostics->gain_clipped = true;
                if (settings_.reduce_rumble || settings_.gain_db != 0) {
                    if (output >= 32767.0f) samples[index] = 32767;
                    else if (output <= -32768.0f) samples[index] = -32768;
                    else samples[index] = static_cast<int16_t>(lroundf(output));
                }
                if (diagnostics) {
                    const int32_t sample = samples[index];
                    const uint32_t amplitude = sample < 0 ? static_cast<uint32_t>(-sample) : static_cast<uint32_t>(sample);
                    if (amplitude > diagnostics->peak) diagnostics->peak = static_cast<uint16_t>(amplitude);
                    diagnostics->sum_squares += static_cast<uint64_t>(static_cast<int64_t>(sample) * sample);
                }
            }
        }
    }

private:
    AudioInputProcessingSettings settings_ = {};
    uint32_t sample_rate_ = 0;
    uint8_t channels_ = 0;
    float gain_ = 1.0f;
    float alpha_ = 0.0f;
    float previous_input_[2] = {};
    float previous_output_[2] = {};
};

#endif

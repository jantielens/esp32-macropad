#pragma once

#include <stddef.h>
#include <stdint.h>

struct AudioInputFormat {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
};

class AudioInputDriver {
public:
    virtual ~AudioInputDriver() {}

    virtual bool inputAvailable() const = 0;
    virtual bool captureStart() = 0;
    virtual size_t readFrames(int16_t* frames, size_t frame_count,
                              uint32_t timeout_ms) = 0;
    virtual void captureStop() = 0;
    virtual AudioInputFormat inputFormat() const = 0;
};

// Returns the input capability associated with the board-selected audio
// driver, or nullptr when the selected output hardware has no input path.
AudioInputDriver* audio_input_driver_create();
#ifndef AUDIO_OUTPUT_DRIVER_H
#define AUDIO_OUTPUT_DRIVER_H

#include <stddef.h>
#include <stdint.h>

class AudioOutputDriver {
public:
    virtual ~AudioOutputDriver() {}

    virtual bool begin(uint32_t sample_rate) = 0;
    virtual bool write(const int16_t* frames, size_t frame_count) = 0;
    virtual void setVolume(uint8_t vol_0_100) = 0;

    virtual void setMuted(bool muted);
    virtual void flush() {}
};

AudioOutputDriver* audio_output_driver_create();

#endif // AUDIO_OUTPUT_DRIVER_H
#ifndef AUDIO_OUTPUT_DRIVER_H
#define AUDIO_OUTPUT_DRIVER_H

#include <stddef.h>
#include <stdint.h>

struct AudioBoardControl {
    bool (*power)(bool enabled) = nullptr;
    bool (*mute)(bool muted) = nullptr;
};

AudioBoardControl audio_board_control_create();

class AudioOutputDriver {
public:
    virtual ~AudioOutputDriver() {}

    virtual bool begin(uint32_t sample_rate) = 0;
    virtual bool write(const int16_t* frames, size_t frame_count) = 0;
    virtual bool setVolume(uint8_t vol_0_100) = 0;

    virtual bool setMuted(bool muted) = 0;
    virtual bool supportsMute() const { return false; }
    virtual bool isAvailable() const = 0;
    virtual bool end() = 0;
    virtual void flush() {}
};

AudioOutputDriver* audio_output_driver_create();

#endif // AUDIO_OUTPUT_DRIVER_H
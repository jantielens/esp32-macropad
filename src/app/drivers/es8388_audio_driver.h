#pragma once

#include "audio_output_driver.h"
#include "driver/i2s_std.h"

class ES8388AudioDriver final : public AudioOutputDriver {
public:
    explicit ES8388AudioDriver(const AudioBoardControl& value) : control(value) {}
    bool begin(uint32_t sample_rate) override;
    bool write(const int16_t* frames, size_t frame_count) override;
    bool setVolume(uint8_t volume) override;
    bool setMuted(bool muted) override;
    bool supportsMute() const override { return control.mute != nullptr; }
    bool isAvailable() const override { return initialized; }
    bool end() override;

private:
    AudioBoardControl control;
    i2s_chan_handle_t tx = nullptr;
    bool running = false;
    bool initialized = false;
};
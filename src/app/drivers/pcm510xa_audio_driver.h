#pragma once

#include "audio_output_driver.h"
#include "driver/i2s_std.h"

class I2SOutputDriver final : public AudioOutputDriver {
public:
    explicit I2SOutputDriver(const AudioBoardControl& value) : control(value) {}
    bool begin(uint32_t sample_rate) override;
    bool write(const int16_t* frames, size_t frame_count) override;
    bool setVolume(uint8_t vol_0_100) override;
    bool setMuted(bool muted) override;
    bool supportsMute() const override { return control.mute != nullptr; }
    bool isAvailable() const override { return initialized; }
    bool end() override;

private:
    AudioBoardControl control;
    i2s_chan_handle_t tx_handle = nullptr;
    uint8_t volume = 100;
    bool initialized = false;
    bool tx_running = false;
    uint32_t sample_rate = 0;
};

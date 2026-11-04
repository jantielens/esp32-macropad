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
    bool needsContinuousIdleOutput() const override { return true; }
    bool needsRebootShutdown() const override { return true; }
    bool isAvailable() const override { return initialized; }
    bool end() override;

private:
    AudioBoardControl control;
    i2s_chan_handle_t tx = nullptr;
    uint8_t silence_phase = 0;
    bool running = false;
    bool codecStarted = false;
    bool initialized = false;
};
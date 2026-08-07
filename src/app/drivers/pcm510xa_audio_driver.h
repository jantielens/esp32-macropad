#pragma once

#include "audio_output_driver.h"
#include "driver/i2s_std.h"

class PCM510xADriver final : public AudioOutputDriver {
public:
    bool begin(uint32_t sample_rate) override;
    bool write(const int16_t* frames, size_t frame_count) override;
    void setVolume(uint8_t vol_0_100) override;
    void setMuted(bool muted) override;

private:
    i2s_chan_handle_t tx_handle = nullptr;
    uint8_t volume = 100;
    bool initialized = false;
};

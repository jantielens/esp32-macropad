#ifndef ES8311_AUDIO_DRIVER_H
#define ES8311_AUDIO_DRIVER_H

#include "audio_output_driver.h"
#include "driver/i2s_types.h"

class ES8311AudioDriver final : public AudioOutputDriver {
public:
    bool begin(uint32_t sample_rate) override;
    bool write(const int16_t* frames, size_t frame_count) override;
    void setVolume(uint8_t vol_0_100) override;

private:
    bool initCodec(uint32_t sample_rate);
    void cleanup();

    i2s_chan_handle_t tx_handle = NULL;
    i2s_chan_handle_t rx_handle = NULL;
};

#endif // ES8311_AUDIO_DRIVER_H
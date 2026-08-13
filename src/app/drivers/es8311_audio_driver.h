#ifndef ES8311_AUDIO_DRIVER_H
#define ES8311_AUDIO_DRIVER_H

#include "board_config.h"
#include "audio_output_driver.h"
#include "driver/i2s_types.h"

#if HAS_AUDIO_INPUT
#include "audio_input_driver.h"
#endif

class ES8311AudioDriver final : public AudioOutputDriver
#if HAS_AUDIO_INPUT
    , public AudioInputDriver
#endif
{
public:
    bool begin(uint32_t sample_rate) override;
    bool write(const int16_t* frames, size_t frame_count) override;
    void setVolume(uint8_t vol_0_100) override;
#if HAS_AUDIO_INPUT
    bool inputAvailable() const override;
    bool captureStart() override;
    size_t readFrames(int16_t* frames, size_t frame_count, uint32_t timeout_ms) override;
    void captureStop() override;
    AudioInputFormat inputFormat() const override;
#endif

private:
    bool initCodec(uint32_t sample_rate);
    void cleanup();

    i2s_chan_handle_t tx_handle = NULL;
#if HAS_AUDIO_INPUT
    i2s_chan_handle_t rx_handle = NULL;
    uint32_t sample_rate = 0;
#endif
};

#endif // ES8311_AUDIO_DRIVER_H
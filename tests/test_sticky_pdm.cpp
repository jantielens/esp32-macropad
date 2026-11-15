#include "audio_input_driver.h"
#include <driver/i2s_pdm.h>
#include <cassert>

bool sticky_hardware_ready() { return true; }

int main() {
    AudioInputDriver* input = audio_input_driver_create();
    assert(input->inputFormat().sample_rate == 16000);
    assert(input->inputFormat().channels == 1 && input->inputFormat().bits_per_sample == 16);

    audio_test_init_fails = true;
    assert(!input->captureStart());
    assert(!audio_test_channel_exists && !audio_test_power);
    audio_test_init_fails = false;
    assert(input->captureStart());
    assert(audio_test_channel_exists && audio_test_power);
    // One descriptor may be filling while the rest buffer a flash write.
    assert((audio_test_channel_config.dma_desc_num - 1) *
           audio_test_channel_config.dma_frame_num * 1000 / input->inputFormat().sample_rate > 158);

    int16_t samples[32];
    audio_test_timeout = true;
    assert(input->readFrames(samples, 32, 20).status == AudioInputReadStatus::Timeout);
    audio_test_timeout = false;
    audio_test_overrun = true;
    assert(input->readFrames(samples, 32, 20).status == AudioInputReadStatus::Overrun);
    assert(input->captureStatus() == AudioInputReadStatus::Overrun);

    audio_test_disable_fails = true;
    assert(!input->captureStop());
    assert(audio_test_channel_exists && !audio_test_power);
    audio_test_disable_fails = false;
    assert(input->captureStop());
    assert(!audio_test_channel_exists && !audio_test_power);
    assert(input->captureStart());
    assert(input->captureStatus() == AudioInputReadStatus::Ok);
    assert(input->captureStop());
}

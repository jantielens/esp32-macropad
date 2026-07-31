#include "drivers/pcm510xa_audio_driver.h"

#include <Arduino.h>
#include "audio_gain.h"
#include "log_manager.h"

#define TAG "Audio"

static constexpr uint32_t kClockSettleMs = 10;
static constexpr uint32_t kMuteSettleMs =
    (150UL * 1000UL + AUDIO_SAMPLE_RATE - 1) / AUDIO_SAMPLE_RATE + 1 + 1;

bool PCM510xADriver::begin(uint32_t sample_rate) {
    setMuted(true);
    pinMode(AUDIO_I2S_MCLK, OUTPUT);
    digitalWrite(AUDIO_I2S_MCLK, LOW);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = AUDIO_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = AUDIO_DMA_FRAME_NUM;
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (err != ESP_OK) {
        LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)AUDIO_I2S_BCLK,
            .ws = (gpio_num_t)AUDIO_I2S_LRCK,
            .dout = (gpio_num_t)AUDIO_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.ws_width = I2S_SLOT_BIT_WIDTH_32BIT;

    err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (err != ESP_OK) {
        LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(err));
        i2s_del_channel(tx_handle);
        tx_handle = nullptr;
        return false;
    }
    err = i2s_channel_enable(tx_handle);
    if (err != ESP_OK) {
        LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
        i2s_del_channel(tx_handle);
        tx_handle = nullptr;
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(kClockSettleMs));
    initialized = true;
    setMuted(false);
    setVolume(volume);
    LOGI(TAG, "PCM510xA: %u Hz, 16-bit data in 32-bit slots", sample_rate);
    return true;
}

bool PCM510xADriver::write(const int16_t* frames, size_t frame_count) {
    if (!tx_handle || !frames || frame_count == 0) return false;

    int16_t scratch[512];
    const size_t total_samples = frame_count * 2;
    const uint8_t vol = volume;  // One gain per call; volume may change from other tasks.
    for (size_t offset = 0; offset < total_samples;) {
        const size_t sample_count = min(total_samples - offset, sizeof(scratch) / sizeof(scratch[0]));
        for (size_t index = 0; index < sample_count; ++index) {
            scratch[index] = audio_gain_apply(frames[offset + index], vol);
        }

        size_t written = 0;
        esp_err_t err = i2s_channel_write(tx_handle, scratch, sample_count * sizeof(int16_t),
                                          &written, portMAX_DELAY);
        if (err != ESP_OK || written != sample_count * sizeof(int16_t)) {
            LOGE(TAG, "I2S write error: %s", esp_err_to_name(err));
            return false;
        }
        offset += sample_count;
    }
    return true;
}

void PCM510xADriver::setVolume(uint8_t vol_0_100) {
    volume = vol_0_100 > 100 ? 100 : vol_0_100;
}

// Intentionally not inherited: the base no-pin guard silently succeeds, which is unsafe for DAC soft-mute.
void PCM510xADriver::setMuted(bool muted) {
    if (!initialized && !muted) return;
    pinMode(AUDIO_PA_PIN, OUTPUT);
    const bool enabled = !muted;
    digitalWrite(AUDIO_PA_PIN, (enabled ^ AUDIO_PA_ACTIVE_LOW) ? HIGH : LOW);
}

// A future teardown must mute and wait kMuteSettleMs with LRCK running.
// It must then disable and delete the I2S channel before any DAC power is removed.
// This firmware has no teardown path: the channel is created once at boot and
// lives for the rest of the firmware run.
AudioOutputDriver* audio_output_driver_create() {
    static PCM510xADriver driver;
    return &driver;
}

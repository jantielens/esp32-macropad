#include "drivers/es8388_audio_driver.h"
#include "i2c_bus.h"
#include "log_manager.h"
#include <Wire.h>

namespace {
bool writeCodec(uint8_t reg, uint8_t value) {
    if (!i2c_bus_lock()) return false;
    Wire.beginTransmission(AUDIO_CODEC_ADDR);
    const bool written = Wire.write(reg) == 1 && Wire.write(value) == 1;
    const bool ok = Wire.endTransmission() == 0 && written;
    i2c_bus_unlock();
    return ok;
}

int readCodec(uint8_t reg) {
    Wire.beginTransmission(AUDIO_CODEC_ADDR);
    if (Wire.write(reg) != 1 || Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(uint8_t(AUDIO_CODEC_ADDR), uint8_t(1)) != 1) return -1;
    return Wire.read();
}
}

bool ES8388AudioDriver::begin(uint32_t sample_rate) {
    if (tx) return initialized && setMuted(true);
    if (sample_rate != 48000 || !setMuted(true)) return false;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    channel.dma_desc_num = AUDIO_DMA_DESC_NUM;
    channel.dma_frame_num = AUDIO_DMA_FRAME_NUM;
    esp_err_t error = i2s_new_channel(&channel, &tx, nullptr);
    if (error != ESP_OK) { end(); return false; }
    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)AUDIO_I2S_MCLK,
            .bclk = (gpio_num_t)AUDIO_I2S_BCLK,
            .ws = (gpio_num_t)AUDIO_I2S_LRCK,
            .dout = (gpio_num_t)AUDIO_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    struct Register { uint8_t address, value; };
    static const Register sequence[] = {
        {0, 0x80}, {0, 0}, {0, 0}, {0, 0x0e}, {1, 0}, {2, 0x0a}, {3, 0xff},
        {4, 0x3c}, {5, 0}, {6, 0}, {7, 0x7c}, {8, 0}, {23, 0x18}, {24, 0},
        {25, 0x20}, {26, 0}, {27, 0}, {28, 0x08}, {29, 0}, {38, 0},
        {39, 0xb8}, {42, 0xb8}, {43, 0x08}, {45, 0}, {46, 0x21}, {47, 0x21},
        {48, 0x21}, {49, 0x21},
    };
    codecStarted = true;
    for (const Register& entry : sequence) {
        if (!writeCodec(entry.address, entry.value)) {
            LOGE("Audio", "ES8388 initialization failed at register 0x%02x", entry.address);
            end();
            return false;
        }
    }
    error = i2s_channel_init_std_mode(tx, &config);
    if (error != ESP_OK) { LOGE("Audio", "ES8388 I2S init failed: %s", esp_err_to_name(error)); end(); return false; }
    error = i2s_channel_enable(tx);
    if (error != ESP_OK) { LOGE("Audio", "ES8388 I2S start failed: %s", esp_err_to_name(error)); end(); return false; }
    running = true;
    vTaskDelay(pdMS_TO_TICKS(50));
    initialized = true;
    int16_t silence[64] = {};
    if (!write(silence, 32)) { end(); return false; }
    return true;
}

bool ES8388AudioDriver::write(const int16_t* frames, size_t frame_count) {
    if (!initialized || !frames || !frame_count) return false;
    size_t written = 0;
    const size_t bytes = frame_count * 2 * sizeof(int16_t);
    const esp_err_t error = i2s_channel_write(tx, frames, bytes, &written, portMAX_DELAY);
    static bool loggedPcm = false;
    if (!loggedPcm) {
        for (size_t sample = 0; sample < frame_count * 2; ++sample) {
            if (frames[sample] != 0) {
                LOGI("Audio", "ES8388 PCM: sample=%d bytes=%u/%u result=%s",
                     int(frames[sample]), unsigned(written), unsigned(bytes), esp_err_to_name(error));
                loggedPcm = true;
                static const uint8_t registers[] = {2, 4, 8, 23, 24, 25, 26, 27, 39, 42, 46, 48};
                int values[sizeof(registers)] = {};
                if (i2c_bus_lock()) {
                    for (size_t index = 0; index < sizeof(registers); ++index)
                        values[index] = readCodec(registers[index]);
                    i2c_bus_unlock();
                    LOGI("Audio", "ES8388 power/format: 02=%d 04=%d 08=%d 23=%d 24=%d 25=%d",
                         values[0], values[1], values[2], values[3], values[4], values[5]);
                    LOGI("Audio", "ES8388 volume/mix: 26=%d 27=%d 39=%d 42=%d 46=%d 48=%d",
                         values[6], values[7], values[8], values[9], values[10], values[11]);
                } else {
                    LOGW("Audio", "ES8388 register readback lock failed");
                }
                break;
            }
        }
    }
    return error == ESP_OK && written == bytes;
}

bool ES8388AudioDriver::setVolume(uint8_t volume) {
    if (!initialized) return false;
    if (volume > 100) volume = 100;
    const uint8_t attenuation = uint16_t(100 - volume) * 192 / 100;
    if (writeCodec(26, attenuation) && writeCodec(27, attenuation)) return true;
    setMuted(true);
    return false;
}

bool ES8388AudioDriver::setMuted(bool muted) {
    if (!control.mute || (!muted && !initialized)) return false;
    return control.mute(muted);
}

bool ES8388AudioDriver::end() {
    bool ok = setMuted(true);
    if (codecStarted) {
        if (!writeCodec(25, 0x24)) ok = false;
        vTaskDelay(pdMS_TO_TICKS(1));
        if (!writeCodec(4, 0xc0) || !writeCodec(2, 0xff)) ok = false;
        codecStarted = false;
    }
    if (running) {
        if (i2s_channel_disable(tx) == ESP_OK) running = false;
        else ok = false;
    }
    if (tx && i2s_del_channel(tx) == ESP_OK) { tx = nullptr; running = false; }
    else if (tx) ok = false;
    initialized = false;
    return ok;
}

AudioOutputDriver* audio_output_driver_create() {
    static ES8388AudioDriver driver(audio_board_control_create());
    return &driver;
}
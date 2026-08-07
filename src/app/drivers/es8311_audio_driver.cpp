#include "drivers/es8311_audio_driver.h"

#include <Wire.h>
#include "driver/i2s_std.h"
#include "i2c_bus.h"
#include "log_manager.h"

#define TAG "Audio"

#define ES8311_RESET_REG00         0x00
#define ES8311_CLK_MANAGER_REG01   0x01
#define ES8311_CLK_MANAGER_REG02   0x02
#define ES8311_CLK_MANAGER_REG03   0x03
#define ES8311_CLK_MANAGER_REG04   0x04
#define ES8311_CLK_MANAGER_REG05   0x05
#define ES8311_CLK_MANAGER_REG06   0x06
#define ES8311_CLK_MANAGER_REG07   0x07
#define ES8311_CLK_MANAGER_REG08   0x08
#define ES8311_SDPIN_REG09         0x09
#define ES8311_SDPOUT_REG0A        0x0A
#define ES8311_SYSTEM_REG0B        0x0B
#define ES8311_SYSTEM_REG0C        0x0C
#define ES8311_SYSTEM_REG0D        0x0D
#define ES8311_SYSTEM_REG0E        0x0E
#define ES8311_SYSTEM_REG10        0x10
#define ES8311_SYSTEM_REG11        0x11
#define ES8311_SYSTEM_REG12        0x12
#define ES8311_SYSTEM_REG13        0x13
#define ES8311_SYSTEM_REG14        0x14
#define ES8311_ADC_REG15           0x15
#define ES8311_ADC_REG16           0x16
#define ES8311_ADC_REG17           0x17
#define ES8311_ADC_REG1B           0x1B
#define ES8311_ADC_REG1C           0x1C
#define ES8311_DAC_REG31           0x31
#define ES8311_DAC_REG32           0x32
#define ES8311_DAC_REG37           0x37
#define ES8311_GPIO_REG44          0x44
#define ES8311_GP_REG45            0x45

struct ES8311Coeff {
    uint32_t mclk_hz;
    uint32_t sample_rate;
    uint8_t pre_div;
    uint8_t pre_multi;
    uint8_t adc_div;
    uint8_t dac_div;
    uint8_t fs_mode;
    uint8_t lrck_h;
    uint8_t lrck_l;
    uint8_t bclk_div;
    uint8_t adc_osr;
    uint8_t dac_osr;
};

static const ES8311Coeff ES8311_COEFFS[] = {
    {6144000, 16000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xFF, 0x04, 0x10, 0x20},
    {12288000, 48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xFF, 0x04, 0x10, 0x10},
};

static constexpr i2s_mclk_multiple_t kEs8311MclkMultiple = I2S_MCLK_MULTIPLE_256;

static const ES8311Coeff* es8311_find_coeff(uint32_t mclk_hz, uint32_t sample_rate) {
    for (const ES8311Coeff& coeff : ES8311_COEFFS) {
        if (coeff.mclk_hz == mclk_hz && coeff.sample_rate == sample_rate) {
            return &coeff;
        }
    }
    return nullptr;
}

static bool es8311_write(uint8_t reg, uint8_t val) {
    i2c_bus_lock();
    Wire.beginTransmission(AUDIO_CODEC_ADDR);
    Wire.write(reg);
    Wire.write(val);
    bool ok = Wire.endTransmission() == 0;
    i2c_bus_unlock();
    return ok;
}

static uint8_t es8311_read(uint8_t reg) {
    i2c_bus_lock();
    Wire.beginTransmission(AUDIO_CODEC_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)AUDIO_CODEC_ADDR, (uint8_t)1);
    uint8_t val = Wire.available() ? Wire.read() : 0xFF;
    i2c_bus_unlock();
    return val;
}

bool ES8311AudioDriver::initCodec(uint32_t sample_rate) {
    const uint32_t mclk_hz = sample_rate * static_cast<uint32_t>(kEs8311MclkMultiple);
    const ES8311Coeff* coeff = es8311_find_coeff(mclk_hz, sample_rate);
    if (!coeff) {
        LOGE(TAG, "No ES8311 coefficients for MCLK=%lu Hz, Fs=%lu Hz", mclk_hz, sample_rate);
        return false;
    }

    es8311_write(ES8311_GPIO_REG44, 0x08);
    es8311_write(ES8311_GPIO_REG44, 0x08);
    es8311_write(ES8311_CLK_MANAGER_REG01, 0x30);
    es8311_write(ES8311_CLK_MANAGER_REG02, 0x00);
    es8311_write(ES8311_CLK_MANAGER_REG03, 0x10);
    es8311_write(ES8311_ADC_REG16, 0x24);
    es8311_write(ES8311_CLK_MANAGER_REG04, 0x10);
    es8311_write(ES8311_CLK_MANAGER_REG05, 0x00);
    es8311_write(ES8311_SYSTEM_REG0B, 0x00);
    es8311_write(ES8311_SYSTEM_REG0C, 0x00);
    es8311_write(ES8311_SYSTEM_REG10, 0x1F);
    es8311_write(ES8311_SYSTEM_REG11, 0x7F);
    es8311_write(ES8311_RESET_REG00, 0x80);

    uint8_t regv = es8311_read(ES8311_RESET_REG00);
    regv &= 0xBF;
    es8311_write(ES8311_RESET_REG00, regv);

    es8311_write(ES8311_CLK_MANAGER_REG01, 0x3F);
    regv = es8311_read(ES8311_CLK_MANAGER_REG01);
    regv &= 0x7F;
    regv &= ~0x40;
    es8311_write(ES8311_CLK_MANAGER_REG01, regv);

    regv = es8311_read(ES8311_CLK_MANAGER_REG02) & 0x07;
    regv |= (coeff->pre_div - 1) << 5;
    uint8_t datmp = (coeff->pre_multi == 1) ? 0 : (coeff->pre_multi == 2) ? 1
                  : (coeff->pre_multi == 4) ? 2 : 3;
    regv |= datmp << 3;
    es8311_write(ES8311_CLK_MANAGER_REG02, regv);

    regv = es8311_read(ES8311_CLK_MANAGER_REG05) & 0x00;
    regv |= (coeff->adc_div - 1) << 4;
    regv |= (coeff->dac_div - 1) << 0;
    es8311_write(ES8311_CLK_MANAGER_REG05, regv);

    regv = es8311_read(ES8311_CLK_MANAGER_REG03) & 0x80;
    regv |= coeff->fs_mode << 6;
    regv |= coeff->adc_osr;
    es8311_write(ES8311_CLK_MANAGER_REG03, regv);

    regv = es8311_read(ES8311_CLK_MANAGER_REG04) & 0x80;
    regv |= coeff->dac_osr;
    es8311_write(ES8311_CLK_MANAGER_REG04, regv);

    regv = es8311_read(ES8311_CLK_MANAGER_REG07) & 0xC0;
    regv |= coeff->lrck_h;
    es8311_write(ES8311_CLK_MANAGER_REG07, regv);
    es8311_write(ES8311_CLK_MANAGER_REG08, coeff->lrck_l);

    regv = es8311_read(ES8311_CLK_MANAGER_REG06) & 0xE0;
    regv |= (coeff->bclk_div - 1);
    regv &= ~0x20;
    es8311_write(ES8311_CLK_MANAGER_REG06, regv);

    es8311_write(ES8311_SYSTEM_REG13, 0x10);
    es8311_write(ES8311_ADC_REG1B, 0x0A);
    es8311_write(ES8311_ADC_REG1C, 0x6A);

    uint8_t dac_iface = es8311_read(ES8311_SDPIN_REG09) & 0xBF;
    dac_iface &= ~(1 << 6);
    es8311_write(ES8311_SDPIN_REG09, dac_iface);

    uint8_t adc_iface = es8311_read(ES8311_SDPOUT_REG0A) & 0xBF;
    adc_iface |= (1 << 6);
    es8311_write(ES8311_SDPOUT_REG0A, adc_iface);

    es8311_write(ES8311_ADC_REG17, 0xBF);
    es8311_write(ES8311_SYSTEM_REG0E, 0x02);
    es8311_write(ES8311_SYSTEM_REG12, 0x00);
    es8311_write(ES8311_SYSTEM_REG14, 0x1A);
    es8311_write(ES8311_SYSTEM_REG0D, 0x01);
    es8311_write(ES8311_ADC_REG15, 0x40);
    es8311_write(ES8311_DAC_REG37, 0x08);
    es8311_write(ES8311_GP_REG45, 0x00);
    es8311_write(ES8311_GPIO_REG44, 0x58);

    dac_iface = es8311_read(ES8311_SDPIN_REG09);
    dac_iface &= 0xF0;
    dac_iface |= 0x0C;
    es8311_write(ES8311_SDPIN_REG09, dac_iface);

    adc_iface = es8311_read(ES8311_SDPOUT_REG0A);
    adc_iface &= 0xF0;
    adc_iface |= 0x0C;
    es8311_write(ES8311_SDPOUT_REG0A, adc_iface);

    regv = es8311_read(ES8311_DAC_REG31) & 0x9F;
    es8311_write(ES8311_DAC_REG31, regv);

    LOGI(TAG, "ES8311 codec initialized (MCLK=%lu Hz, Fs=%lu Hz)", mclk_hz, sample_rate);
    return true;
}

bool ES8311AudioDriver::begin(uint32_t sample_rate) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = AUDIO_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = AUDIO_DMA_FRAME_NUM;
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
    if (err != ESP_OK) {
        LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)AUDIO_I2S_MCLK,
            .bclk = (gpio_num_t)AUDIO_I2S_BCLK,
            .ws   = (gpio_num_t)AUDIO_I2S_LRCK,
            .dout = (gpio_num_t)AUDIO_I2S_DOUT,
            .din  = (gpio_num_t)AUDIO_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = kEs8311MclkMultiple;

    err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (err != ESP_OK) {
        LOGE(TAG, "i2s_channel_init_std_mode(TX) failed: %s", esp_err_to_name(err));
        cleanup();
        return false;
    }
    err = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (err != ESP_OK) {
        LOGE(TAG, "i2s_channel_init_std_mode(RX) failed: %s", esp_err_to_name(err));
        cleanup();
        return false;
    }
    err = i2s_channel_enable(tx_handle);
    if (err != ESP_OK) {
        LOGE(TAG, "i2s_channel_enable(TX) failed: %s", esp_err_to_name(err));
        cleanup();
        return false;
    }
    err = i2s_channel_enable(rx_handle);
    if (err != ESP_OK) {
        LOGE(TAG, "i2s_channel_enable(RX) failed: %s", esp_err_to_name(err));
        cleanup();
        return false;
    }

    LOGI(TAG, "I2S TX: %u Hz, 16-bit stereo, MCLK=%lu Hz (%lux)",
         sample_rate, sample_rate * static_cast<uint32_t>(kEs8311MclkMultiple),
         static_cast<uint32_t>(kEs8311MclkMultiple));
    vTaskDelay(pdMS_TO_TICKS(50));
    if (!initCodec(sample_rate)) {
        LOGE(TAG, "ES8311 init failed");
        cleanup();
        return false;
    }
    return true;
}

bool ES8311AudioDriver::write(const int16_t* frames, size_t frame_count) {
    if (!tx_handle || !frames || frame_count == 0) return false;
    size_t written;
    esp_err_t err = i2s_channel_write(tx_handle, frames, frame_count * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    if (err != ESP_OK) {
        LOGE(TAG, "I2S write error: %s", esp_err_to_name(err));
        return false;
    }
    return written == frame_count * 2 * sizeof(int16_t);
}

void ES8311AudioDriver::setVolume(uint8_t vol_0_100) {
    uint8_t reg_val = (uint8_t)((uint16_t)vol_0_100 * 255 / 100);
    bool ok = es8311_write(ES8311_DAC_REG32, reg_val);
    LOGD(TAG, "Volume: %u%% -> REG32=0x%02X %s", vol_0_100, reg_val, ok ? "OK" : "FAIL");
}

void ES8311AudioDriver::cleanup() {
    if (rx_handle) {
        i2s_channel_disable(rx_handle);
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
    }
    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
    }
}

AudioOutputDriver* audio_output_driver_create() {
    static ES8311AudioDriver driver;
    return &driver;
}
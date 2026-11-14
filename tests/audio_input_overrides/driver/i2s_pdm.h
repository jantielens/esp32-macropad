#pragma once
#include "gpio.h"
#include <cstddef>
#include <cstring>

using i2s_chan_handle_t = void*;
struct i2s_event_data_t {};
using audio_test_callback = bool (*)(i2s_chan_handle_t, i2s_event_data_t*, void*);
struct i2s_event_callbacks_t { audio_test_callback on_recv_q_ovf = nullptr; };
struct i2s_chan_config_t { uint32_t dma_desc_num = 6; uint32_t dma_frame_num = 240; };
struct i2s_pdm_rx_config_t {
    struct {} clk_cfg;
    struct {} slot_cfg;
    struct { gpio_num_t clk; gpio_num_t din; } gpio_cfg;
};
#define I2S_NUM_0 0
#define I2S_ROLE_MASTER 0
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_MONO 1
#define I2S_CHANNEL_DEFAULT_CONFIG(...) i2s_chan_config_t{}
#define I2S_PDM_RX_CLK_DEFAULT_CONFIG(...) {}
#define I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(...) {}

inline bool audio_test_init_fails = false;
inline bool audio_test_disable_fails = false;
inline bool audio_test_overrun = false;
inline bool audio_test_timeout = false;
inline bool audio_test_channel_exists = false;
inline i2s_chan_config_t audio_test_channel_config;
inline audio_test_callback audio_test_on_overrun = nullptr;
inline void* audio_test_callback_context = nullptr;
inline esp_err_t i2s_new_channel(const i2s_chan_config_t* config, i2s_chan_handle_t*, i2s_chan_handle_t* rx) {
    audio_test_channel_config = *config;
    audio_test_channel_exists = true;
    *rx = &audio_test_channel_exists;
    return ESP_OK;
}
inline esp_err_t i2s_channel_init_pdm_rx_mode(i2s_chan_handle_t, const i2s_pdm_rx_config_t* config) {
    if (config->gpio_cfg.clk != GPIO_NUM_19 || config->gpio_cfg.din != GPIO_NUM_20) return 3;
    return audio_test_init_fails ? 3 : ESP_OK;
}
inline esp_err_t i2s_channel_register_event_callback(i2s_chan_handle_t,
        const i2s_event_callbacks_t* callbacks, void* context) {
    audio_test_on_overrun = callbacks->on_recv_q_ovf;
    audio_test_callback_context = context;
    return ESP_OK;
}
inline esp_err_t i2s_channel_enable(i2s_chan_handle_t) { return ESP_OK; }
inline esp_err_t i2s_channel_disable(i2s_chan_handle_t) {
    return audio_test_disable_fails ? 3 : ESP_OK;
}
inline esp_err_t i2s_del_channel(i2s_chan_handle_t) {
    audio_test_channel_exists = false;
    return ESP_OK;
}
inline esp_err_t i2s_channel_read(i2s_chan_handle_t channel, void* data, size_t count,
                                  size_t* bytes_read, uint32_t) {
    if (audio_test_overrun) {
        audio_test_overrun = false;
        audio_test_on_overrun(channel, nullptr, audio_test_callback_context);
    }
    if (audio_test_timeout) {
        *bytes_read = 0;
        return ESP_ERR_TIMEOUT;
    }
    std::memset(data, 0, count);
    *bytes_read = count;
    return ESP_OK;
}

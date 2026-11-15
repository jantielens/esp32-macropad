#include "audio_input_driver.h"
#include "log_manager.h"
#include "boards/reterminal-sticky-interactive/sticky_hardware.h"

#include <driver/gpio.h>
#include <driver/i2s_pdm.h>
#include <freertos/FreeRTOS.h>

namespace {
constexpr char TAG[] = "StickyMic";
constexpr uint32_t kSampleRate = 16000;
constexpr gpio_num_t kClockPin = GPIO_NUM_19;
constexpr gpio_num_t kDataPin = GPIO_NUM_20;
constexpr gpio_num_t kPowerPin = GPIO_NUM_38;

class StickyPdmInput final : public AudioInputDriver {
public:
    bool inputAvailable() const override { return sticky_hardware_ready(); }

    bool captureStart() override {
        if (!inputAvailable()) return false;
        if (running) return true;
        if (!cleanup()) return false;

        portENTER_CRITICAL(&mux);
        overrun = false;
        portEXIT_CRITICAL(&mux);
        reported_overrun = false;
        capture_failed = false;

        i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        // A 158 ms flash write has been observed; leave room for longer stalls.
        channel.dma_desc_num = 16;
        channel.dma_frame_num = 512;
        esp_err_t err = i2s_new_channel(&channel, nullptr, &rx);
        if (err == ESP_OK) {
            i2s_pdm_rx_config_t config = {};
            config.clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(kSampleRate);
            config.slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
            config.gpio_cfg.clk = kClockPin;
            config.gpio_cfg.din = kDataPin;
            err = i2s_channel_init_pdm_rx_mode(rx, &config);
        }
        if (err == ESP_OK) {
            i2s_event_callbacks_t callbacks = {};
            callbacks.on_recv_q_ovf = on_overrun;
            err = i2s_channel_register_event_callback(rx, &callbacks, this);
        }
        if (err == ESP_OK) err = gpio_set_level(kPowerPin, 1);
        if (err == ESP_OK) {
            err = i2s_channel_enable(rx);
            running = err == ESP_OK;
        }
        if (err == ESP_OK) {
            int16_t settling_samples[320];
            size_t bytes_read = 0;
            err = i2s_channel_read(rx, settling_samples, sizeof(settling_samples),
                                   &bytes_read, 100);
            if (err == ESP_OK && bytes_read != sizeof(settling_samples)) {
                err = ESP_ERR_INVALID_SIZE;
            }
        }
        if (err == ESP_OK && !has_overrun()) return true;
        if (err == ESP_OK) LOGE(TAG, "Microphone overrun during startup");
        else LOGE(TAG, "Microphone start failed: %s", esp_err_to_name(err));
        if (!cleanup()) LOGE(TAG, "Microphone cleanup after start failure failed");
        return false;
    }

    AudioInputReadResult readFrames(int16_t* frames, size_t frame_count, uint32_t timeout_ms) override {
        const AudioInputReadStatus status = captureStatus();
        if (!running || !frames || !frame_count || status != AudioInputReadStatus::Ok) {
            report_overrun();
            return {0, status == AudioInputReadStatus::Ok ? AudioInputReadStatus::Error : status};
        }
        size_t bytes_read = 0;
        const esp_err_t err = i2s_channel_read(
            rx, frames, frame_count * sizeof(int16_t), &bytes_read, timeout_ms);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            LOGE(TAG, "Microphone read failed: %s", esp_err_to_name(err));
            capture_failed = true;
            return {0, AudioInputReadStatus::Error};
        }
        if (has_overrun()) {
            report_overrun();
            return {0, AudioInputReadStatus::Overrun};
        }
        if (bytes_read % sizeof(int16_t)) {
            LOGE(TAG, "Microphone returned an incomplete sample");
            capture_failed = true;
            return {0, AudioInputReadStatus::Error};
        }
        return {bytes_read / sizeof(int16_t),
                err == ESP_ERR_TIMEOUT ? AudioInputReadStatus::Timeout : AudioInputReadStatus::Ok};
    }

    AudioInputReadStatus captureStatus() override {
        if (has_overrun()) return AudioInputReadStatus::Overrun;
        return capture_failed ? AudioInputReadStatus::Error : AudioInputReadStatus::Ok;
    }

    bool captureStop() override {
        if (cleanup()) return true;
        LOGE(TAG, "Microphone stop failed");
        return false;
    }

    AudioInputFormat inputFormat() const override { return {kSampleRate, 1, 16}; }

private:
    static bool IRAM_ATTR on_overrun(i2s_chan_handle_t, i2s_event_data_t*, void* context) {
        auto* self = static_cast<StickyPdmInput*>(context);
        portENTER_CRITICAL_ISR(&self->mux);
        self->overrun = true;
        portEXIT_CRITICAL_ISR(&self->mux);
        return false;
    }

    bool has_overrun() {
        portENTER_CRITICAL(&mux);
        const bool result = overrun;
        portEXIT_CRITICAL(&mux);
        return result;
    }

    void report_overrun() {
        if (!reported_overrun && has_overrun()) {
            LOGE(TAG, "Microphone DMA overrun; capture must be restarted");
            reported_overrun = true;
        }
    }

    bool cleanup() {
        bool ok = true;
        if (rx && running) {
            const esp_err_t err = i2s_channel_disable(rx);
            if (err == ESP_OK) running = false;
            else {
                LOGE(TAG, "I2S disable failed: %s", esp_err_to_name(err));
                ok = false;
            }
        }
        if (rx && !running) {
            const esp_err_t err = i2s_del_channel(rx);
            if (err == ESP_OK) rx = nullptr;
            else {
                LOGE(TAG, "I2S release failed: %s", esp_err_to_name(err));
                ok = false;
            }
        }
        const esp_err_t err = gpio_set_level(kPowerPin, 0);
        if (err != ESP_OK) {
            LOGE(TAG, "Microphone power off failed: %s", esp_err_to_name(err));
            ok = false;
        }
        return ok;
    }

    i2s_chan_handle_t rx = nullptr;
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    bool running = false;
    bool overrun = false;
    bool reported_overrun = false;
    bool capture_failed = false;
};
}

AudioInputDriver* audio_input_driver_create() {
    static StickyPdmInput driver;
    return &driver;
}

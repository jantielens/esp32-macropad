#include "audio.h"

#if HAS_AUDIO

#include <Wire.h>
#include <math.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "log_manager.h"
#include "i2c_bus.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#define TAG "Audio"

// ---------------------------------------------------------------------------
// ES8311 register addresses
// ---------------------------------------------------------------------------
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
#define ES8311_GP_REG45            0x45

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static i2s_chan_handle_t tx_handle = NULL;
static bool audio_initialized = false;
static uint8_t current_volume = 70; // 0-100
static const uint32_t SAMPLE_RATE = 16000;
static const uint32_t MCLK_FREQ = SAMPLE_RATE * 384;

// Coefficient table entry for MCLK=6144000, Rate=16000
static const uint8_t COEFF_PRE_DIV   = 0x03;
static const uint8_t COEFF_PRE_MULTI = 0x02;
static const uint8_t COEFF_ADC_DIV   = 0x01;
static const uint8_t COEFF_DAC_DIV   = 0x01;
static const uint8_t COEFF_FS_MODE   = 0x00;
static const uint8_t COEFF_LRCK_H    = 0x00;
static const uint8_t COEFF_LRCK_L    = 0xFF;
static const uint8_t COEFF_BCLK_DIV  = 0x04;
static const uint8_t COEFF_ADC_OSR   = 0x10;
static const uint8_t COEFF_DAC_OSR   = 0x10;

// ---------------------------------------------------------------------------
// Async playback queue
// ---------------------------------------------------------------------------
#define AUDIO_PATTERN_MAX_LEN 128
#define AUDIO_QUEUE_DEPTH     2

struct AudioCommand {
    char pattern[AUDIO_PATTERN_MAX_LEN];
    uint8_t volume_override; // 0 = use current
    bool loop;               // true = repeat until stop
};

static QueueHandle_t audio_queue = NULL;
static TaskHandle_t audio_task_handle = NULL;
// Cross-task flags: written by audio_enqueue()/audio_stop() (caller task),
// read/written by audio_task (FreeRTOS task).  volatile provides visibility;
// no mutex needed because the only race (g_playing read in audio_enqueue vs
// g_playing write in audio_task) is benign — a spurious g_stop_requested=true
// when nothing is playing is harmlessly cleared on the next queue receive.
static volatile bool g_stop_requested = false;
static volatile bool g_playing = false;

// ---------------------------------------------------------------------------
// ES8311 I2C helpers (Wire bus 0, protected by i2c_bus mutex)
// ---------------------------------------------------------------------------
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

// Map 0-100 user volume to ES8311 REG32 (0x00-0xFF)
static void es8311_apply_volume(uint8_t vol_0_100) {
    uint8_t reg_val = (uint8_t)((uint16_t)vol_0_100 * 255 / 100);
    bool ok = es8311_write(ES8311_DAC_REG32, reg_val);
    LOGD(TAG, "Volume: %u%% -> REG32=0x%02X %s", vol_0_100, reg_val, ok ? "OK" : "FAIL");
}

// ---------------------------------------------------------------------------
// ES8311 init — matches Espressif reference driver
// ---------------------------------------------------------------------------
static bool es8311_init_codec() {
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

    // Slave mode
    uint8_t regv = es8311_read(ES8311_RESET_REG00);
    regv &= 0xBF;
    es8311_write(ES8311_RESET_REG00, regv);

    // Enable all clocks, MCLK from pin
    es8311_write(ES8311_CLK_MANAGER_REG01, 0x3F);
    regv = es8311_read(ES8311_CLK_MANAGER_REG01);
    regv &= 0x7F;
    regv &= ~0x40;
    es8311_write(ES8311_CLK_MANAGER_REG01, regv);

    // Clock dividers from coefficient table
    regv = es8311_read(ES8311_CLK_MANAGER_REG02) & 0x07;
    regv |= (COEFF_PRE_DIV - 1) << 5;
    uint8_t datmp = (COEFF_PRE_MULTI == 1) ? 0 : (COEFF_PRE_MULTI == 2) ? 1
                  : (COEFF_PRE_MULTI == 4) ? 2 : 3;
    regv |= datmp << 3;
    es8311_write(ES8311_CLK_MANAGER_REG02, regv);

    regv = es8311_read(ES8311_CLK_MANAGER_REG05) & 0x00;
    regv |= (COEFF_ADC_DIV - 1) << 4;
    regv |= (COEFF_DAC_DIV - 1) << 0;
    es8311_write(ES8311_CLK_MANAGER_REG05, regv);

    regv = es8311_read(ES8311_CLK_MANAGER_REG03) & 0x80;
    regv |= COEFF_FS_MODE << 6;
    regv |= COEFF_ADC_OSR;
    es8311_write(ES8311_CLK_MANAGER_REG03, regv);

    regv = es8311_read(ES8311_CLK_MANAGER_REG04) & 0x80;
    regv |= COEFF_DAC_OSR;
    es8311_write(ES8311_CLK_MANAGER_REG04, regv);

    regv = es8311_read(ES8311_CLK_MANAGER_REG07) & 0xC0;
    regv |= COEFF_LRCK_H;
    es8311_write(ES8311_CLK_MANAGER_REG07, regv);
    es8311_write(ES8311_CLK_MANAGER_REG08, COEFF_LRCK_L);

    regv = es8311_read(ES8311_CLK_MANAGER_REG06) & 0xE0;
    regv |= (COEFF_BCLK_DIV - 1);
    regv &= ~0x20;
    es8311_write(ES8311_CLK_MANAGER_REG06, regv);

    es8311_write(ES8311_SYSTEM_REG13, 0x10);
    es8311_write(ES8311_ADC_REG1B, 0x0A);
    es8311_write(ES8311_ADC_REG1C, 0x6A);

    // Power up DAC path (CODEC_MODE_DECODE)
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
    es8311_write(ES8311_DAC_REG37, 0x48);
    es8311_write(ES8311_GP_REG45, 0x00);

    // Format: I2S Philips, 16-bit
    dac_iface = es8311_read(ES8311_SDPIN_REG09);
    dac_iface &= 0xFC;
    dac_iface |= 0x0C;
    es8311_write(ES8311_SDPIN_REG09, dac_iface);

    adc_iface = es8311_read(ES8311_SDPOUT_REG0A);
    adc_iface &= 0xFC;
    adc_iface |= 0x0C;
    es8311_write(ES8311_SDPOUT_REG0A, adc_iface);

    // Unmute DAC
    regv = es8311_read(ES8311_DAC_REG31) & 0x9F;
    es8311_write(ES8311_DAC_REG31, regv);

    LOGI(TAG, "ES8311 codec initialized (MCLK=%lu Hz, Fs=%lu Hz)", MCLK_FREQ, SAMPLE_RATE);
    return true;
}

// ---------------------------------------------------------------------------
// Tone generation — play one segment (freq Hz for duration_ms)
// ---------------------------------------------------------------------------
static void play_tone(uint16_t freq_hz, uint16_t duration_ms) {
    if (!tx_handle) return;

    uint32_t total_samples = (uint32_t)SAMPLE_RATE * duration_ms / 1000;
    if (total_samples == 0) return;

    static const size_t FRAMES_PER_CHUNK = 512;
    int16_t buf[FRAMES_PER_CHUNK * 2];

    if (freq_hz == 0) {
        // Silence: write zeros
        memset(buf, 0, sizeof(buf));
        uint32_t frames_done = 0;
        while (frames_done < total_samples) {
            size_t chunk = (total_samples - frames_done < FRAMES_PER_CHUNK)
                         ? (total_samples - frames_done) : FRAMES_PER_CHUNK;
            size_t bytes = chunk * 2 * sizeof(int16_t);
            size_t written;
            i2s_channel_write(tx_handle, buf, bytes, &written, portMAX_DELAY);
            frames_done += chunk;
        }
        return;
    }

    float phase_inc = 2.0f * M_PI * freq_hz / SAMPLE_RATE;
    float phase = 0.0f;
    const float amplitude = 32767.0f;
    const uint32_t fade_len = (total_samples > 400) ? 200 : total_samples / 4;

    uint32_t frames_done = 0;
    while (frames_done < total_samples) {
        size_t chunk = (total_samples - frames_done < FRAMES_PER_CHUNK)
                     ? (total_samples - frames_done) : FRAMES_PER_CHUNK;

        for (size_t i = 0; i < chunk; i++) {
            uint32_t g = frames_done + i;
            float env = 1.0f;
            if (g < fade_len) {
                env = (float)g / fade_len;
            } else if (g > total_samples - fade_len) {
                env = (float)(total_samples - g) / fade_len;
            }
            int16_t sample = (int16_t)(sinf(phase) * amplitude * env);
            buf[i * 2]     = sample;
            buf[i * 2 + 1] = sample;
            phase += phase_inc;
            if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
        }

        size_t bytes = chunk * 2 * sizeof(int16_t);
        size_t written;
        i2s_channel_write(tx_handle, buf, bytes, &written, portMAX_DELAY);
        frames_done += chunk;
    }
}

// ---------------------------------------------------------------------------
// Beep pattern parser
//   Space-delimited: "freq:dur" for tones, bare "dur" for silence gaps
//   e.g. "1000:200 100 1000:200" = beep, 100ms gap, beep
// ---------------------------------------------------------------------------
static void play_pattern(const char* pattern) {
    if (!pattern || !pattern[0]) {
        play_tone(1000, 200); // default beep
        return;
    }

    char buf[AUDIO_PATTERN_MAX_LEN];
    strlcpy(buf, pattern, sizeof(buf));

    char* saveptr = NULL;
    char* tok = strtok_r(buf, " ", &saveptr);
    while (tok) {
        if (g_stop_requested) return;
        char* colon = strchr(tok, ':');
        if (colon) {
            *colon = '\0';
            uint16_t freq = (uint16_t)atoi(tok);
            uint16_t dur  = (uint16_t)atoi(colon + 1);
            if (dur > 0 && dur <= 10000) {
                play_tone(freq, dur);
            }
        } else {
            uint16_t dur = (uint16_t)atoi(tok);
            if (dur > 0 && dur <= 10000) {
                play_tone(0, dur); // silence gap
            }
        }
        tok = strtok_r(NULL, " ", &saveptr);
    }
}

// ---------------------------------------------------------------------------
// Audio task — processes queued beep commands
// PA stays on permanently after first use (avoids settle-time beep clipping).
// ---------------------------------------------------------------------------
static void audio_task(void* param) {
    AudioCommand cmd;

    for (;;) {
        if (xQueueReceive(audio_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            g_stop_requested = false;
            g_playing = true;

            uint8_t play_vol = current_volume;
            if (cmd.volume_override > 0 && cmd.volume_override <= 100) {
                play_vol = cmd.volume_override;
            }
            LOGD(TAG, "Play: vol=%u%% (override=%u, device=%u) loop=%d", play_vol, cmd.volume_override, current_volume, cmd.loop);
            es8311_apply_volume(play_vol);

            if (cmd.loop) {
                while (!g_stop_requested) {
                    play_pattern(cmd.pattern);
                }
            } else {
                play_pattern(cmd.pattern);
            }

            // Restore device volume if overridden
            if (cmd.volume_override > 0 && cmd.volume_override <= 100) {
                es8311_apply_volume(current_volume);
            }
            g_playing = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void audio_init(uint8_t initial_volume) {
    LOGI(TAG, "Initializing audio (ESP-IDF I2S + ES8311)");

    current_volume = (initial_volume > 100) ? 100 : initial_volume;

    // PA on permanently (no idle management — avoids settle-time clipping)
    if (AUDIO_PA_PIN >= 0) {
        pinMode(AUDIO_PA_PIN, OUTPUT);
        digitalWrite(AUDIO_PA_PIN, HIGH);
        LOGI(TAG, "PA enabled (GPIO%d)", AUDIO_PA_PIN);
    }

    // I2S channel setup
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (err != ESP_OK) {
        LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
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
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;

    err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (err != ESP_OK) {
        LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(err));
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
        return;
    }

    err = i2s_channel_enable(tx_handle);
    if (err != ESP_OK) {
        LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
        return;
    }

    LOGI(TAG, "I2S TX: %u Hz, 16-bit stereo, MCLK=%lu Hz (384x)", SAMPLE_RATE, MCLK_FREQ);

    // Initialize codec
    if (!es8311_init_codec()) {
        LOGE(TAG, "ES8311 init failed");
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
        return;
    }

    // Set initial volume
    es8311_apply_volume(current_volume);

    // Create command queue and audio task
    audio_queue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(AudioCommand));
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 2, &audio_task_handle, 1);

    audio_initialized = true;
    LOGI(TAG, "Audio ready (volume=%u%%, PA always-on)", current_volume);
}

void audio_set_volume(uint8_t vol_0_100) {
    if (vol_0_100 > 100) vol_0_100 = 100;
    current_volume = vol_0_100;
    if (audio_initialized) {
        es8311_apply_volume(current_volume);
    }
    LOGI(TAG, "Volume: %u%%", current_volume);
}

uint8_t audio_get_volume() {
    return current_volume;
}

static void audio_enqueue(const char* pattern, uint8_t volume_override, bool loop) {
    if (!audio_initialized) {
        LOGW(TAG, "Audio not initialized");
        return;
    }

    // If starting a new command, stop any current loop first
    if (g_playing) {
        g_stop_requested = true;
    }

    // Flush queue
    AudioCommand discard;
    while (xQueueReceive(audio_queue, &discard, 0) == pdTRUE) {}

    AudioCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    if (pattern && pattern[0]) {
        strlcpy(cmd.pattern, pattern, AUDIO_PATTERN_MAX_LEN);
    }
    cmd.volume_override = volume_override;
    cmd.loop = loop;

    xQueueSend(audio_queue, &cmd, portMAX_DELAY);
}

void audio_beep(const char* pattern, uint8_t volume_override) {
    audio_enqueue(pattern, volume_override, false);
}

void audio_play_loop(const char* pattern, uint8_t volume_override) {
    audio_enqueue(pattern, volume_override, true);
}

void audio_stop() {
    if (!audio_initialized) return;
    g_stop_requested = true;
    // Flush queued commands
    AudioCommand discard;
    while (xQueueReceive(audio_queue, &discard, 0) == pdTRUE) {}
    LOGD(TAG, "Stop requested");
}

bool audio_is_playing() {
    return g_playing;
}

#endif // HAS_AUDIO

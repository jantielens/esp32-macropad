#include "music_analysis.h"

#if HAS_MUSIC_ANALYSIS && HAS_AUDIO && HAS_SOUND_PLAYER

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pad_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace {

static constexpr uint8_t BAND_COUNT = 8;
static constexpr uint16_t BAND_MIN_HZ[BAND_COUNT] = {60, 120, 250, 500, 1000, 2000, 4000, 8000};
static constexpr uint16_t BAND_MAX_HZ[BAND_COUNT] = {120, 250, 500, 1000, 2000, 4000, 8000, 16000};
static constexpr uint32_t ANALYSIS_SAMPLE_RATE = AUDIO_SAMPLE_RATE;
static constexpr uint32_t ANALYSIS_REFRESH_US = 50000; // 20 Hz maximum

static volatile uint8_t g_demand = MUSIC_ANALYSIS_NONE;
static portMUX_TYPE g_demand_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_snapshot_mux = portMUX_INITIALIZER_UNLOCKED;
static MusicAnalysisSnapshot g_snapshot = {};
static uint64_t g_window_sum_sq = 0;
static uint32_t g_window_peak = 0;
static uint32_t g_window_frames = 0;
static uint64_t g_last_publish_us = 0;
static uint8_t g_smoothed_rms = 0;
static uint8_t g_smoothed_peak = 0;
static uint8_t g_bands[BAND_COUNT] = {};
static uint8_t g_band_levels[BAND_COUNT] = {};
static int16_t* g_window = nullptr;
static size_t g_window_count = 0;
static constexpr size_t ANALYSIS_WINDOW_FRAMES = 2048;

static uint8_t pcm_to_level(uint32_t amplitude) {
    if (amplitude > 32767U) amplitude = 32767U;
    return (uint8_t)((amplitude * 100U + 16383U) / 32767U);
}

static uint8_t spectrum_level(float amplitude) {
    if (amplitude <= 1.0f) return 0;
    const float dbfs = 20.0f * log10f(amplitude / 32767.0f);
    const float level = (dbfs + 60.0f) * (100.0f / 60.0f);
    if (level <= 0.0f) return 0;
    if (level >= 100.0f) return 100;
    return (uint8_t)(level + 0.5f);
}

static float goertzel_amplitude(const int16_t* samples, size_t count, float hz) {
    const float omega = 6.28318530718f * hz / (float)ANALYSIS_SAMPLE_RATE;
    const float coeff = 2.0f * cosf(omega);
    float s1 = 0.0f, s2 = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const float s0 = (float)samples[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const float magnitude = sqrtf(fmaxf(0.0f, s1 * s1 + s2 * s2 - coeff * s1 * s2));
    return (2.0f * magnitude) / (float)count;
}

static uint32_t isqrt_u64(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = 1ULL << 62;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

static uint64_t now_us() {
    return (uint64_t)esp_timer_get_time();
}

static void publish_window() {
    if (g_window_frames == 0) return;
    const uint32_t rms = isqrt_u64(g_window_sum_sq / g_window_frames);
    const uint8_t rms_level = pcm_to_level(rms);
    const uint8_t peak_level = pcm_to_level(g_window_peak);

    if ((music_analysis_get_demand() & MUSIC_ANALYSIS_BANDS) && g_window_count > 0) {
        for (uint8_t band = 0; band < BAND_COUNT; ++band) {
            // Probe three logarithmically spaced points per band. A single
            // narrow Goertzel bin makes ordinary music look artificially
            // quiet when its energy falls between the bin and its harmonics.
            const float low = (float)BAND_MIN_HZ[band];
            const float high = (float)BAND_MAX_HZ[band];
            const float middle = sqrtf(low * high);
            float amplitude = goertzel_amplitude(g_window, g_window_count, low);
            const float middle_amplitude = goertzel_amplitude(g_window, g_window_count, middle);
            const float high_amplitude = goertzel_amplitude(g_window, g_window_count, high);
            if (middle_amplitude > amplitude) amplitude = middle_amplitude;
            if (high_amplitude > amplitude) amplitude = high_amplitude;
            const uint8_t target = spectrum_level(amplitude);
            // Fast attack, slower release: bars react to new energy quickly
            // but remain visible long enough for the display to paint them.
            g_band_levels[band] = target >= g_band_levels[band]
                ? (uint8_t)((g_band_levels[band] + target * 3U + 2U) / 4U)
                : (uint8_t)((g_band_levels[band] * 3U + target + 2U) / 4U);
            g_bands[band] = g_band_levels[band];
        }
    }

    // Fast attack, slow release keeps level meters readable without lagging.
    g_smoothed_rms = (uint8_t)((g_smoothed_rms * 3U + rms_level + 2U) / 4U);
    g_smoothed_peak = peak_level > g_smoothed_peak
        ? peak_level
        : (uint8_t)((g_smoothed_peak * 15U + peak_level + 8U) / 16U);

    portENTER_CRITICAL(&g_snapshot_mux);
    g_snapshot.rms = g_smoothed_rms;
    g_snapshot.peak = g_smoothed_peak;
    memcpy(g_snapshot.bands, g_bands, sizeof(g_bands));
    portEXIT_CRITICAL(&g_snapshot_mux);

    g_window_sum_sq = 0;
    g_window_peak = 0;
    g_window_frames = 0;
}

static void collect_key(const char* key, uint8_t* demand) {
    if (strcmp(key, "rms") == 0) *demand |= MUSIC_ANALYSIS_RMS;
    else if (strcmp(key, "peak") == 0) *demand |= MUSIC_ANALYSIS_PEAK;
    else if (strncmp(key, "band.", 5) == 0) {
        const int band = atoi(key + 5);
        if (band >= 0 && band < BAND_COUNT) *demand |= MUSIC_ANALYSIS_BANDS;
    }
}

static void collect_action(const ButtonAction& action, uint8_t* demand) {
    const char* fields[5] = {};
    if (strcmp(action.type, ACTION_TYPE_SCREEN) == 0) {
        fields[0] = action.payload.screen.screen_id;
    } else if (strcmp(action.type, ACTION_TYPE_MQTT) == 0) {
        fields[0] = action.payload.mqtt.mqtt_topic;
        fields[1] = action.payload.mqtt.mqtt_payload;
    } else if (strcmp(action.type, ACTION_TYPE_KEY) == 0) {
        fields[0] = action.payload.key.key_sequence;
    } else if (strcmp(action.type, ACTION_TYPE_VOLUME) == 0) {
        fields[0] = action.payload.volume.volume_value;
    } else if (strcmp(action.type, ACTION_TYPE_BRIGHTNESS) == 0) {
        fields[0] = action.payload.brightness.brightness_value;
    } else if (strcmp(action.type, ACTION_TYPE_TIMER) == 0) {
        fields[0] = action.payload.timer.timer_value;
    } else if (strcmp(action.type, ACTION_TYPE_NOTIFY) == 0) {
        fields[0] = action.payload.notify.notify_text;
        fields[1] = action.payload.notify.notify_duration_ms;
        fields[2] = action.payload.notify.notify_text_color;
        fields[3] = action.payload.notify.notify_bg_color;
        fields[4] = action.payload.notify.notify_border_color;
    } else if (strcmp(action.type, ACTION_TYPE_VISUAL_ALERT) == 0) {
        fields[0] = action.payload.visual_alert.va_color;
    }
    for (const char* field : fields) {
        if (field) music_analysis_collect_demand(field, demand);
    }
}

} // namespace

void music_analysis_init() {
    if (!g_window) {
        g_window = (int16_t*)heap_caps_malloc(
            ANALYSIS_WINDOW_FRAMES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!g_window) {
            g_window = (int16_t*)heap_caps_malloc(
                ANALYSIS_WINDOW_FRAMES * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    music_analysis_reset();
}

void music_analysis_set_demand(uint8_t demand) {
    portENTER_CRITICAL(&g_demand_mux);
    g_demand = demand;
    portEXIT_CRITICAL(&g_demand_mux);
    if (demand == MUSIC_ANALYSIS_NONE) music_analysis_reset();
}

uint8_t music_analysis_get_demand() {
    portENTER_CRITICAL(&g_demand_mux);
    const uint8_t demand = g_demand;
    portEXIT_CRITICAL(&g_demand_mux);
    return demand;
}

void music_analysis_set_playing(bool playing) {
    portENTER_CRITICAL(&g_snapshot_mux);
    g_snapshot.playing = playing;
    portEXIT_CRITICAL(&g_snapshot_mux);
    if (!playing) music_analysis_reset();
}

void music_analysis_reset() {
    g_window_sum_sq = 0;
    g_window_peak = 0;
    g_window_frames = 0;
    g_window_count = 0;
    g_last_publish_us = 0;
    g_smoothed_rms = 0;
    g_smoothed_peak = 0;
    memset(g_bands, 0, sizeof(g_bands));
    memset(g_band_levels, 0, sizeof(g_band_levels));
    portENTER_CRITICAL(&g_snapshot_mux);
    g_snapshot = {};
    portEXIT_CRITICAL(&g_snapshot_mux);
}

void music_analysis_process(const int16_t* stereo_frames, size_t frame_count) {
    const uint8_t demand = music_analysis_get_demand();
    if (demand == MUSIC_ANALYSIS_NONE || !stereo_frames || frame_count == 0) return;

    for (size_t i = 0; i < frame_count; ++i) {
        const int32_t mono = ((int32_t)stereo_frames[i * 2] + stereo_frames[i * 2 + 1]) / 2;
        const uint32_t amplitude = (uint32_t)abs(mono);
        g_window_sum_sq += (uint64_t)(mono * (int64_t)mono);
        if (amplitude > g_window_peak) g_window_peak = amplitude;
        g_window_frames++;
        if ((demand & MUSIC_ANALYSIS_BANDS) && g_window &&
            g_window_count < ANALYSIS_WINDOW_FRAMES) {
            g_window[g_window_count++] = (int16_t)mono;
        }
    }

    const uint64_t now = now_us();
    if (now - g_last_publish_us >= ANALYSIS_REFRESH_US) {
        publish_window();
        g_last_publish_us = now;
        g_window_count = 0;
    }
}

void music_analysis_get_snapshot(MusicAnalysisSnapshot* out) {
    if (!out) return;
    portENTER_CRITICAL(&g_snapshot_mux);
    *out = g_snapshot;
    portEXIT_CRITICAL(&g_snapshot_mux);
}

void music_analysis_collect_demand(const char* templ, uint8_t* demand) {
    if (!templ || !demand) return;
    const char* p = templ;
    while ((p = strstr(p, "[music:analysis.")) != nullptr) {
        p += strlen("[music:analysis.");
        const char* end = strchr(p, ']');
        if (!end) break;
        char key[16] = {};
        const size_t len = (size_t)(end - p) < sizeof(key) - 1
            ? (size_t)(end - p) : sizeof(key) - 1;
        memcpy(key, p, len);
        key[len] = '\0';
        collect_key(key, demand);
        p = end + 1;
    }
}

void music_analysis_collect_pad_demand(const PadConfig* config, uint8_t* demand) {
    if (!config || !demand) return;
    music_analysis_collect_demand(config->bg_color, demand);
    for (uint8_t i = 0; i < config->binding_count; ++i) {
        music_analysis_collect_demand(config->bindings[i].value, demand);
    }
    for (uint8_t i = 0; i < config->pad_action_count; ++i) {
        collect_action(config->pad_actions[i], demand);
    }
    for (uint8_t i = 0; i < config->button_count; ++i) {
        const ScreenButtonConfig& button = config->buttons[i];
        const char* fields[] = {
            button.label_top, button.label_center, button.label_bottom,
            button.label_top_color_bind, button.label_center_color_bind,
            button.label_bottom_color_bind, button.bg_color, button.fg_color,
            button.border_color, button.border_width, button.corner_radius,
            button.btn_state,
        };
        for (const char* field : fields) music_analysis_collect_demand(field, demand);
        for (uint8_t j = 0; j < MAX_WIDGET_BINDINGS; ++j) {
            music_analysis_collect_demand(button.widget.data_binding[j], demand);
        }
        for (uint8_t j = 0; j < button.action_count; ++j) collect_action(button.actions[j], demand);
        for (uint8_t j = 0; j < button.lp_action_count; ++j) collect_action(button.lp_actions[j], demand);
    }
}

void music_analysis_rebuild_demand() {
    PadConfig* config = (PadConfig*)heap_caps_malloc(
        sizeof(PadConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!config) {
        config = (PadConfig*)heap_caps_malloc(
            sizeof(PadConfig), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!config) return;

    uint8_t demand = MUSIC_ANALYSIS_NONE;
    for (uint8_t page = 0; page < MAX_PADS; ++page) {
        if (pad_config_load(page, config)) {
            music_analysis_collect_pad_demand(config, &demand);
        }
    }
    music_analysis_set_demand(demand);
    heap_caps_free(config);
}

#endif

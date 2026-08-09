#include "audio_input.h"

#if HAS_AUDIO_INPUT

#include <Arduino.h>
#include "log_manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace {

constexpr char TAG[] = "AudioInput";
constexpr uint32_t kMeterFrames = 960;
constexpr uint32_t kMeterLeaseUs = 250000;
constexpr uint32_t kMeterSnapshotMaxAgeUs = 100000;

AudioInputDriver* g_driver = nullptr;
TaskHandle_t g_capture_owner = nullptr;
portMUX_TYPE g_capture_mux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_meter_task = nullptr;
portMUX_TYPE g_meter_mux = portMUX_INITIALIZER_UNLOCKED;
uint64_t g_meter_deadline_us = 0;
AudioInputMeterSnapshot g_meter_snapshot = {};
uint64_t g_meter_last_sample_us = 0;
AudioInputFormat g_meter_format = {};
int16_t* g_meter_samples = nullptr;

bool owned_by_current_task() {
    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&g_capture_mux);
    const bool owns_capture = g_capture_owner == caller;
    portEXIT_CRITICAL(&g_capture_mux);
    return owns_capture;
}

uint8_t pcm_to_level(uint32_t amplitude) {
    if (amplitude > 32767U) amplitude = 32767U;
    return (uint8_t)((amplitude * 100U + 16383U) / 32767U);
}

uint32_t isqrt_u64(uint64_t value) {
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

bool meter_requested() {
    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&g_meter_mux);
    const bool requested = now_us < g_meter_deadline_us;
    portEXIT_CRITICAL(&g_meter_mux);
    return requested;
}

void meter_set_inactive() {
    portENTER_CRITICAL(&g_meter_mux);
    g_meter_snapshot = {};
    g_meter_last_sample_us = 0;
    portEXIT_CRITICAL(&g_meter_mux);
}

void meter_publish(const int16_t* samples, size_t frame_count, uint8_t channels) {
    uint64_t sum_sq = 0;
    uint32_t peak = 0;
    for (size_t frame = 0; frame < frame_count; ++frame) {
        int32_t mono = 0;
        for (uint8_t channel = 0; channel < channels; ++channel) {
            mono += samples[frame * channels + channel];
        }
        mono /= channels;
        const uint32_t amplitude = mono < 0 ? (uint32_t)-mono : (uint32_t)mono;
        sum_sq += (uint64_t)(mono * (int64_t)mono);
        if (amplitude > peak) peak = amplitude;
    }

    portENTER_CRITICAL(&g_meter_mux);
    g_meter_snapshot.rms = pcm_to_level(isqrt_u64(sum_sq / frame_count));
    g_meter_snapshot.peak = pcm_to_level(peak);
    g_meter_snapshot.active = true;
    g_meter_last_sample_us = (uint64_t)esp_timer_get_time();
    portEXIT_CRITICAL(&g_meter_mux);
}

void meter_task(void*) {
    bool capture_active = false;
    for (;;) {
        if (!meter_requested()) {
            if (capture_active) {
                audio_input_stop_capture();
                capture_active = false;
            }
            meter_set_inactive();
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        if (!capture_active) {
            capture_active = audio_input_start_capture();
            if (!capture_active) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
        }

        const size_t frame_count = audio_input_read_frames(g_meter_samples, kMeterFrames, 50);
        if (frame_count) {
            meter_publish(g_meter_samples, frame_count, g_meter_format.channels);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

} // namespace

bool audio_input_available() {
    if (!g_driver) g_driver = audio_input_driver_create();
    return g_driver && g_driver->inputAvailable();
}

bool audio_input_start_capture() {
    if (!audio_input_available()) return false;

    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&g_capture_mux);
    if (g_capture_owner && g_capture_owner != caller) {
        portEXIT_CRITICAL(&g_capture_mux);
        return false;
    }
    if (g_capture_owner == caller) {
        portEXIT_CRITICAL(&g_capture_mux);
        return true;
    }
    g_capture_owner = caller;
    portEXIT_CRITICAL(&g_capture_mux);

    if (g_driver->captureStart()) return true;

    portENTER_CRITICAL(&g_capture_mux);
    if (g_capture_owner == caller) g_capture_owner = nullptr;
    portEXIT_CRITICAL(&g_capture_mux);
    LOGW(TAG, "Capture start rejected by driver");
    return false;
}

size_t audio_input_read_frames(int16_t* frames, size_t frame_count, uint32_t timeout_ms) {
    if (!frames || frame_count == 0 || !audio_input_available() || !owned_by_current_task()) {
        return 0;
    }
    return g_driver->readFrames(frames, frame_count, timeout_ms);
}

void audio_input_stop_capture() {
    if (!g_driver || !owned_by_current_task()) return;

    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    g_driver->captureStop();
    portENTER_CRITICAL(&g_capture_mux);
    if (g_capture_owner == caller) g_capture_owner = nullptr;
    portEXIT_CRITICAL(&g_capture_mux);
}

AudioInputFormat audio_input_format() {
    return audio_input_available() ? g_driver->inputFormat() : AudioInputFormat{};
}

void audio_input_meter_init() {
    if (g_meter_task || !audio_input_available()) return;

    const AudioInputFormat format = audio_input_format();
    if (format.channels == 0 || format.channels > 2 || format.bits_per_sample != 16) {
        LOGE(TAG, "Unsupported microphone meter format");
        return;
    }
    g_meter_samples = (int16_t*)heap_caps_malloc(
        kMeterFrames * format.channels * sizeof(int16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!g_meter_samples) {
        LOGE(TAG, "Microphone meter buffer allocation failed");
        return;
    }
    g_meter_format = format;
    if (xTaskCreatePinnedToCore(meter_task, "MicMeter", 4096, nullptr, 2,
                                &g_meter_task, 0) != pdPASS) {
        LOGE(TAG, "Microphone meter task creation failed");
        heap_caps_free(g_meter_samples);
        g_meter_samples = nullptr;
        g_meter_task = nullptr;
    }
}

void audio_input_meter_request() {
    portENTER_CRITICAL(&g_meter_mux);
    g_meter_deadline_us = (uint64_t)esp_timer_get_time() + kMeterLeaseUs;
    portEXIT_CRITICAL(&g_meter_mux);
    if (g_meter_task) xTaskNotifyGive(g_meter_task);
}

bool audio_input_meter_get_snapshot(AudioInputMeterSnapshot* out) {
    if (!out) return false;
    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&g_meter_mux);
    const bool fresh = g_meter_snapshot.active &&
        now_us - g_meter_last_sample_us <= kMeterSnapshotMaxAgeUs;
    *out = fresh ? g_meter_snapshot : AudioInputMeterSnapshot{};
    portEXIT_CRITICAL(&g_meter_mux);
    return fresh;
}

#endif // HAS_AUDIO_INPUT
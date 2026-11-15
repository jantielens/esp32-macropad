#include "audio_input.h"

#if HAS_AUDIO_INPUT

#include <Arduino.h>
#include "audio_pcm_level.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "rtos_task_utils.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace {

constexpr char TAG[] = "AudioInput";
constexpr uint32_t kMeterFrames = 960;
constexpr uint32_t kMeterLeaseUs = 250000;
constexpr uint32_t kMeterSnapshotMaxAgeUs = 100000;
constexpr uint32_t kMeterHandoffUs = 300000;

AudioInputDriver* g_driver = nullptr;
TaskHandle_t g_capture_owner = nullptr;
TaskHandle_t g_capture_waiter = nullptr;
bool g_capture_running = false;
bool g_capture_needs_cleanup = false;
portMUX_TYPE g_capture_mux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_meter_task = nullptr;
RtosTaskPsramAlloc g_meter_task_allocation = {};
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

bool meter_requested() {
    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&g_meter_mux);
    const bool requested = now_us < g_meter_deadline_us;
    portEXIT_CRITICAL(&g_meter_mux);
    return requested;
}

bool meter_should_yield() {
    portENTER_CRITICAL(&g_capture_mux);
    const bool waiting = g_capture_waiter != nullptr;
    portEXIT_CRITICAL(&g_capture_mux);
    return waiting || ota_activity_is_active();
}

void meter_set_inactive() {
    portENTER_CRITICAL(&g_meter_mux);
    g_meter_snapshot = {};
    g_meter_last_sample_us = 0;
    portEXIT_CRITICAL(&g_meter_mux);
}

void meter_publish(const int16_t* samples, size_t frame_count, uint8_t channels) {
    const AudioPcmLevels levels = audio_pcm_levels(samples, frame_count, channels);
    const uint64_t sampled_at_us = (uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&g_meter_mux);
    g_meter_snapshot.rms = levels.rms;
    g_meter_snapshot.peak = levels.peak;
    g_meter_snapshot.active = true;
    g_meter_last_sample_us = sampled_at_us;
    portEXIT_CRITICAL(&g_meter_mux);
}

void meter_task(void*) {
    bool capture_active = false;
    for (;;) {
        if (!capture_active && audio_input_capture_owned() && !audio_input_stop_capture()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (!meter_requested() || meter_should_yield()) {
            if (capture_active) {
                if (!audio_input_stop_capture()) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }
                capture_active = false;
            }
            meter_set_inactive();
            if (meter_should_yield()) vTaskDelay(pdMS_TO_TICKS(10));
            else ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        if (!capture_active) {
            capture_active = audio_input_start_capture();
            if (!capture_active) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
        }

        const AudioInputReadResult read = audio_input_read_frames(g_meter_samples, kMeterFrames, 50);
        if (read.status == AudioInputReadStatus::Error || read.status == AudioInputReadStatus::Overrun) {
            meter_set_inactive();
            if (audio_input_stop_capture()) capture_active = false;
            vTaskDelay(pdMS_TO_TICKS(10));
        } else if (read.frames) {
            meter_publish(g_meter_samples, read.frames, g_meter_format.channels);
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

} // namespace

bool audio_input_available() {
    portENTER_CRITICAL(&g_capture_mux);
    AudioInputDriver* driver = g_driver;
    portEXIT_CRITICAL(&g_capture_mux);
    if (!driver) {
        driver = audio_input_driver_create();
        portENTER_CRITICAL(&g_capture_mux);
        if (!g_driver) g_driver = driver;
        driver = g_driver;
        portEXIT_CRITICAL(&g_capture_mux);
    }
    return driver && driver->inputAvailable();
}

bool audio_input_capture_owned() { return owned_by_current_task(); }

bool audio_input_start_capture() {
    if (!audio_input_available()) return false;

    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    const uint64_t deadline = (uint64_t)esp_timer_get_time() + kMeterHandoffUs;
    bool acquired = false;
    for (;;) {
        portENTER_CRITICAL(&g_capture_mux);
        if ((!g_capture_owner || g_capture_owner == caller) &&
            (!g_capture_waiter || g_capture_waiter == caller)) {
            if (!g_capture_owner) {
                g_capture_owner = caller;
                acquired = true;
            }
            if (g_capture_waiter == caller) g_capture_waiter = nullptr;
            const bool running = g_capture_running;
            const bool needs_cleanup = g_capture_needs_cleanup;
            portEXIT_CRITICAL(&g_capture_mux);
            if (needs_cleanup) {
                if (!audio_input_pause_capture()) return false;
                acquired = true;
            } else if (running) {
                return true;
            }
            break;
        }
        if (caller == g_meter_task || g_capture_owner != g_meter_task ||
            (g_capture_waiter && g_capture_waiter != caller)) {
            portEXIT_CRITICAL(&g_capture_mux);
            return false;
        }
        g_capture_waiter = caller;
        portEXIT_CRITICAL(&g_capture_mux);
        if (g_meter_task) xTaskNotifyGive(g_meter_task);
        if ((uint64_t)esp_timer_get_time() >= deadline) {
            portENTER_CRITICAL(&g_capture_mux);
            if (g_capture_waiter == caller) g_capture_waiter = nullptr;
            portEXIT_CRITICAL(&g_capture_mux);
            LOGW(TAG, "Microphone meter handoff timed out");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (g_driver->captureStart()) {
        portENTER_CRITICAL(&g_capture_mux);
        g_capture_running = true;
        portEXIT_CRITICAL(&g_capture_mux);
        return true;
    }

    const bool cleaned = g_driver->captureStop();
    if (!cleaned) {
        portENTER_CRITICAL(&g_capture_mux);
        g_capture_needs_cleanup = true;
        portEXIT_CRITICAL(&g_capture_mux);
    }
    if (acquired && cleaned) {
        portENTER_CRITICAL(&g_capture_mux);
        if (g_capture_owner == caller) g_capture_owner = nullptr;
        portEXIT_CRITICAL(&g_capture_mux);
    }
    if (!cleaned) LOGE(TAG, "Capture cleanup failed; reservation retained");
    LOGW(TAG, "Capture start rejected by driver");
    return false;
}

AudioInputReadResult audio_input_read_frames(int16_t* frames, size_t frame_count, uint32_t timeout_ms) {
    if (!frames || frame_count == 0 || !audio_input_available() || !owned_by_current_task()) {
        return {0, AudioInputReadStatus::Error};
    }
    return g_driver->readFrames(frames, frame_count, timeout_ms);
}

AudioInputReadStatus audio_input_capture_status() {
    if (!g_driver || !owned_by_current_task()) return AudioInputReadStatus::Error;
    return g_driver->captureStatus();
}

bool audio_input_pause_capture() {
    if (!g_driver || !owned_by_current_task()) return false;
    if (!g_driver->captureStop()) {
        portENTER_CRITICAL(&g_capture_mux);
        g_capture_needs_cleanup = true;
        portEXIT_CRITICAL(&g_capture_mux);
        LOGE(TAG, "Capture stop failed; reservation retained");
        return false;
    }
    portENTER_CRITICAL(&g_capture_mux);
    g_capture_running = false;
    g_capture_needs_cleanup = false;
    portEXIT_CRITICAL(&g_capture_mux);
    return true;
}

bool audio_input_stop_capture() {
    if (!audio_input_pause_capture()) return false;

    const TaskHandle_t caller = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&g_capture_mux);
    if (g_capture_owner == caller) g_capture_owner = nullptr;
    portEXIT_CRITICAL(&g_capture_mux);
    return true;
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
    const size_t sample_bytes = kMeterFrames * format.channels * sizeof(int16_t);
    g_meter_samples = static_cast<int16_t*>(heap_caps_malloc(sample_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    const bool buffer_in_psram = g_meter_samples != nullptr;
    if (!g_meter_samples) {
        LOGW(TAG, "PSRAM unavailable; allocating microphone meter buffer in internal RAM");
        g_meter_samples = static_cast<int16_t*>(heap_caps_malloc(sample_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (!g_meter_samples) {
        LOGE(TAG, "Microphone meter buffer allocation failed");
        return;
    }
    g_meter_format = format;
    bool created = rtos_create_task_psram_stack_pinned(
        meter_task, "MicMeter", 4096, nullptr, 2, &g_meter_task, &g_meter_task_allocation, 0);
    const bool stack_in_psram = created;
    if (!created) {
        LOGW(TAG, "PSRAM unavailable; allocating microphone meter task in internal RAM");
        created = rtos_create_task_internal_stack_pinned(
            meter_task, "MicMeter", 4096, nullptr, 2, &g_meter_task, &g_meter_task_allocation, 0);
    }
    if (!created) {
        LOGE(TAG, "Microphone meter task creation failed");
        heap_caps_free(g_meter_samples);
        g_meter_samples = nullptr;
        g_meter_task = nullptr;
    } else {
        LOGI(TAG, "Meter stack=%s buffer=%s", stack_in_psram ? "PSRAM" : "internal",
             buffer_in_psram ? "PSRAM" : "internal");
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
    const bool ota_active = ota_activity_is_active();
    portENTER_CRITICAL(&g_capture_mux);
    const bool reserved = (g_capture_owner && g_capture_owner != g_meter_task) ||
                          g_capture_waiter;
    portENTER_CRITICAL(&g_meter_mux);
    const bool fresh = !reserved && !ota_active && g_meter_snapshot.active &&
        now_us - g_meter_last_sample_us <= kMeterSnapshotMaxAgeUs;
    *out = fresh ? g_meter_snapshot : AudioInputMeterSnapshot{};
    portEXIT_CRITICAL(&g_meter_mux);
    portEXIT_CRITICAL(&g_capture_mux);
    return fresh;
}

#endif // HAS_AUDIO_INPUT
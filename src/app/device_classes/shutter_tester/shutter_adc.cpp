#include "shutter_adc.h"

#if IS_SHUTTER_TESTER

#include "log_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_adc/adc_continuous.h>
#include <esp_timer.h>
#include <soc/soc_caps.h>
#include <esp_heap_caps.h>
#include <string.h>

#define TAG "ShutterADC"

// DMA conversion frame size — used for both the driver config and the DMA read buffer.
// The ESP-IDF P4 adc_continuous driver refuses to deliver frames smaller than 4096
// bytes on this configuration (256/1024/2048 produce only "read timeout" warnings,
// no data). 4096 bytes = 1024 sample-sets per frame, ~37 ms latency at 27.7 kSPS in
// 1-sensor mode. Acceptable for this application; cannot be reduced further without
// hitting the driver's minimum.
#define SHUTTER_ADC_FRAME_SIZE 4096

// ============================================================================
// ADC Channel Mapping
// ============================================================================
// Pattern config channels — these are the logical channel numbers passed to the
// ADC continuous driver.  On ESP32-P4 the DMA output may report different
// (physical) channel numbers, so we build the reverse lookup dynamically at
// runtime from the first DMA frame.
//
// Only the first s_active_count entries are used; the rest are unused.
static const adc_channel_t s_all_channels[SHUTTER_SENSOR_MAX] = {
    ADC_CHANNEL_0,  // Sensor S1
    ADC_CHANNEL_1,  // Sensor S2
    ADC_CHANNEL_2,  // Sensor S3
    ADC_CHANNEL_3,  // Sensor S4
    // S5-S9 not mapped to local ADC channels (reserved for future offload)
    (adc_channel_t)0, (adc_channel_t)0, (adc_channel_t)0,
    (adc_channel_t)0, (adc_channel_t)0,
};

// GPIO pin for each sensor position (S1=index 0, S2=index 1, …).
// Used by set_slot_mapping() to convert GPIO→ADC channel.
static const int s_gpio_pins[SHUTTER_SENSOR_MAX] = {
    SHUTTER_ADC_PIN_S1, SHUTTER_ADC_PIN_S2, SHUTTER_ADC_PIN_S3,
    SHUTTER_ADC_PIN_S4, SHUTTER_ADC_PIN_S5, SHUTTER_ADC_PIN_S6,
    SHUTTER_ADC_PIN_S7, SHUTTER_ADC_PIN_S8, SHUTTER_ADC_PIN_S9,
};

// Active channel array — populated by set_slot_mapping() or defaulting to
// s_all_channels[0..N-1].  s_active_channels[slot] = ADC channel for that
// logical slot.  Used by init_adc_continuous() for pattern config and by the
// reverse-map builder to assign DMA channels to logical slots.
static adc_channel_t s_active_channels[SHUTTER_SENSOR_MAX];
static bool s_has_slot_mapping = false;

// Active sensor count set by shutter_adc_init().
static uint8_t s_active_count = 0;

// Reverse lookup: DMA-reported channel number → sensor index (0..SENSOR_COUNT-1).
// Entries not matching a configured channel are set to -1.
// Built once by the ADC task after the first DMA read.
static int8_t s_ch_to_sensor[SOC_ADC_MAX_CHANNEL_NUM];
static bool s_ch_map_ready = false;

// ============================================================================
// Module State
// ============================================================================

static adc_continuous_handle_t s_adc_handle = nullptr;
static bool s_available = false;
static volatile uint16_t s_threshold = SHUTTER_DEFAULT_THRESHOLD;
static TaskHandle_t s_task_handle = nullptr;
static volatile uint32_t s_capture_id = 0;  // Monotonic; incremented on each finalized capture

// Double-buffered capture: the task writes to s_back, then swaps to s_front.
// The LVGL task reads from s_front (protected by portMUX).
static ShutterCapture s_capture_front;
static ShutterCapture s_capture_back;
static portMUX_TYPE s_capture_mux = portMUX_INITIALIZER_UNLOCKED;

// Ring buffer for pre-trigger lookback (per channel, s_active_count slots used).
static uint16_t* s_ring_buf[SHUTTER_SENSOR_MAX] = {};
static uint32_t s_ring_head = 0;
static uint32_t s_ring_size = 0;

// Startup self-calibration window: read DMA frames for this many ms to measure
// the *actual* per-sensor sample rate. The configured aggregate rate is capped
// to the ADC driver's validated continuous-mode range, and calibration records
// the delivered per-sensor rate for measurement timing.
#define SHUTTER_CALIB_WINDOW_MS 1000

// Capture state machine.
enum CaptureState : uint8_t {
    CAPTURE_STOPPED,    // Worker task parked, DMA resources released — no internal-DMA RAM held
    CAPTURE_CALIBRATING, // Measuring actual sample rate at startup; no trigger logic
    CAPTURE_IDLE,       // Waiting for trigger
    CAPTURE_ACTIVE,     // Pulse detected, recording
    CAPTURE_COOLDOWN,   // Post-trigger silence, waiting to finalize
    CAPTURE_ALIGNMENT,  // Live sensor readout for positioning; no trigger logic
};

enum PostCalibrationMode : uint8_t {
    POST_CAL_IDLE,
    POST_CAL_ALIGNMENT,
};

// Written by the ADC task and read by action/binding/web callers across cores.
// Starts in STOPPED because shutter_adc_init() now only configures; the engine
// is brought up by shutter_adc_start() (via shutter_capture_acquire).
static volatile CaptureState s_state = CAPTURE_STOPPED;

// Lifecycle synchronization. The worker task always lives — it parks on
// s_run_sem while STOPPED, and signals s_start_done_sem after attempting
// resource allocation (s_start_result holds the outcome). s_parked_sem is
// posted when the task has fully torn down DMA and is about to block again.
// s_lifecycle_mux serializes concurrent start/stop calls from feature code.
static SemaphoreHandle_t s_run_sem        = nullptr;  // task waits here while parked
static SemaphoreHandle_t s_start_done_sem = nullptr;  // task posts after start attempt
static SemaphoreHandle_t s_parked_sem     = nullptr;  // task posts after teardown completes
static SemaphoreHandle_t s_lifecycle_mux  = nullptr;  // serializes start()/stop()
static volatile bool     s_start_result   = false;
static volatile bool     s_stop_requested = false;

// File-static so shutter_adc_stop() can free it during teardown.
static uint8_t* s_read_buf = nullptr;
static volatile PostCalibrationMode s_post_calibration_mode = POST_CAL_IDLE;
static uint32_t s_post_silence_count = 0;  // Consecutive above-threshold samples after pulse end
static uint32_t s_post_capture_count = 0;  // Samples recorded after pulse end
static uint8_t  s_trigger_debounce = 0;  // Consecutive below-threshold sample counter
static bool s_idle_logged = false;  // Log idle ADC once after boot/capture

// Cross-core alignment request flags (written by API callers, read by ADC task).
static volatile bool s_alignment_requested = false;
static volatile bool s_alignment_exit_requested = false;
static volatile bool s_recalibrate_requested = false;

// Per-sensor boot-calibrated dark baselines (computed during CAPTURE_CALIBRATING).
// Used by alignment mode for percentage calculation.
static uint16_t s_baseline[SHUTTER_SENSOR_MAX] = {};
static uint32_t s_baseline_sum[SHUTTER_SENSOR_MAX] = {};
static uint32_t s_baseline_count = 0;

// Alignment mode double-buffered reading (same pattern as s_capture_front/back).
#include "shutter_capture.h"  // for ShutterAlignmentReading
static ShutterAlignmentReading s_align_front;
static ShutterAlignmentReading s_align_back;
static portMUX_TYPE s_align_mux = portMUX_INITIALIZER_UNLOCKED;

// Alignment decimation accumulators.
static uint32_t s_align_accum[SHUTTER_SENSOR_MAX] = {};
static uint32_t s_align_accum_count = 0;
#define SHUTTER_ALIGN_DECIMATION_SAMPLES 20  // ~1ms window at 20.8 kHz

// Self-calibration state — written by ADC task during CAPTURE_CALIBRATING,
// then read by finalize_capture() and exposed via shutter_adc_get_sample_rate_hz().
// `s_actual_per_sensor_hz` is read from the LVGL/web tasks via the public getter, so
// it is `volatile` to prevent the compiler from caching a stale value across the task
// boundary. 32-bit aligned float reads/writes are atomic on ESP32 — no mutex needed.
// Initialized to the configured rate so the very first frame already has a sane value
// in the unlikely event calibration is skipped.
static volatile float s_actual_per_sensor_hz = (float)SHUTTER_SAMPLE_RATE_HZ;
static int64_t  s_calib_start_us       = 0;
static int64_t  s_calib_count_start_us = 0;  // µs anchor after DMA backlog drained
static uint32_t s_calib_sample_count   = 0;  // Count of complete sample-sets seen during calibration
static uint32_t s_calib_ch0_count      = 0;  // Per-channel rate counter (sensor 0 arrivals)

// Continuous background recalibration during CAPTURE_IDLE.
// The ADC task runs steady-state during idle, so by counting samples against
// a µs-precise wall-clock window we can refine s_actual_per_sensor_hz to far
// better precision than the boot-only window. Reset on every capture
// transition so we only ever measure clean idle periods.
static int64_t  s_recalib_drain_anchor_us = 0;  // µs anchor for the 100ms drain phase
static int64_t  s_recalib_start_us       = 0;  // µs anchor for the (post-drain) counting window
static uint32_t s_recalib_sample_count   = 0;
static uint32_t s_recalib_ch0_count      = 0;  // Per-channel rate counter (sensor 0 arrivals)
static int64_t  s_recalib_next_update_us = 0;  // µs elapsed-since-start at which to log next
static bool     s_recalib_first          = true; // First recalib accepts unconditionally
static constexpr int64_t RECALIB_DRAIN_US = 100000;             // 100ms post-reset DMA-burst drain
static constexpr int64_t RECALIB_UPDATE_INTERVAL_US = 5000000;  // 5s between updates

static void reset_background_recalibration() {
    s_recalib_drain_anchor_us = 0;
    s_recalib_start_us = 0;
    s_recalib_sample_count = 0;
    s_recalib_ch0_count = 0;
    s_recalib_next_update_us = 0;
    s_recalib_first = true;
}

static void reset_calibration_accumulators() {
    s_calib_start_us = 0;
    s_calib_count_start_us = 0;
    s_calib_sample_count = 0;
    s_calib_ch0_count = 0;
    s_baseline_count = 0;
    for (int c = 0; c < s_active_count; c++) {
        s_baseline_sum[c] = 0;
    }
    reset_background_recalibration();
    s_idle_logged = false;
}

static void enter_alignment_mode() {
    s_alignment_requested = false;
    memset(&s_align_back, 0, sizeof(s_align_back));
    memset(s_align_accum, 0, sizeof(s_align_accum));
    s_align_accum_count = 0;
    reset_background_recalibration();
    s_state = CAPTURE_ALIGNMENT;
}

// ============================================================================
// PSRAM Allocation Helpers
// ============================================================================

static uint16_t* alloc_sample_buffer(uint32_t count) {
    return (uint16_t*)heap_caps_malloc(count * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
}

static bool allocate_buffers() {
    // Ring buffers for pre-trigger lookback
    s_ring_size = SHUTTER_PRE_TRIGGER_SAMPLES;
    for (int i = 0; i < s_active_count; i++) {
        s_ring_buf[i] = alloc_sample_buffer(s_ring_size);
        if (!s_ring_buf[i]) {
            LOGE(TAG, "Failed to allocate ring buffer for sensor %d", i);
            return false;
        }
        memset(s_ring_buf[i], 0xFF, s_ring_size * sizeof(uint16_t));
    }

    // Capture buffers (front + back, per active sensor only)
    for (int i = 0; i < s_active_count; i++) {
        s_capture_front.sensors[i].samples = alloc_sample_buffer(SHUTTER_CAPTURE_MAX_SAMPLES);
        s_capture_back.sensors[i].samples  = alloc_sample_buffer(SHUTTER_CAPTURE_MAX_SAMPLES);
        if (!s_capture_front.sensors[i].samples || !s_capture_back.sensors[i].samples) {
            LOGE(TAG, "Failed to allocate capture buffer for sensor %d", i);
            return false;
        }
    }
    return true;
}

// ============================================================================
// ADC Continuous Driver Setup
// ============================================================================

static bool init_adc_continuous() {
    adc_continuous_handle_cfg_t handle_cfg = {};
    handle_cfg.max_store_buf_size = 20 * 1024;
    handle_cfg.conv_frame_size    = SHUTTER_ADC_FRAME_SIZE;

    esp_err_t err = adc_continuous_new_handle(&handle_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        LOGE(TAG, "adc_continuous_new_handle failed: %s", esp_err_to_name(err));
        return false;
    }

    // Configure channels — only s_active_count are active.
    // Use slot-mapped channels if a mapping was set, else default identity.
    adc_digi_pattern_config_t patterns[SHUTTER_SENSOR_MAX] = {};
    for (int i = 0; i < s_active_count; i++) {
        patterns[i].atten    = ADC_ATTEN_DB_12;
        patterns[i].channel  = s_active_channels[i];
        patterns[i].unit     = ADC_UNIT_2;
        patterns[i].bit_width = ADC_BITWIDTH_12;
    }

    adc_continuous_config_t cont_cfg = {};
    cont_cfg.pattern_num    = s_active_count;
    cont_cfg.adc_pattern    = patterns;
    uint32_t requested_total_hz = (uint32_t)SHUTTER_SAMPLE_RATE_HZ * (uint32_t)s_active_count;
    uint32_t configured_total_hz = requested_total_hz;
    if (configured_total_hz > (uint32_t)SHUTTER_ADC_TOTAL_SAMPLE_RATE_HZ) {
        configured_total_hz = (uint32_t)SHUTTER_ADC_TOTAL_SAMPLE_RATE_HZ;
        LOGW(TAG, "ADC rate capped: requested %lu Hz total for %d channel(s), using %lu Hz total",
             (unsigned long)requested_total_hz, s_active_count, (unsigned long)configured_total_hz);
    }
    cont_cfg.sample_freq_hz = configured_total_hz;
    cont_cfg.conv_mode      = ADC_CONV_SINGLE_UNIT_2;
    cont_cfg.format         = ADC_DIGI_OUTPUT_FORMAT_TYPE2;

    err = adc_continuous_config(s_adc_handle, &cont_cfg);
    if (err != ESP_OK) {
        LOGE(TAG, "adc_continuous_config failed: %s", esp_err_to_name(err));
        return false;
    }

    LOGI(TAG, "ADC continuous configured: %d channel(s) @ %lu Hz total",
         s_active_count, (unsigned long)cont_cfg.sample_freq_hz);
    return true;
}

// Tear down DMA resources allocated by start_dma(). Idempotent.
// Called from the worker task only — ensures all DMA frees happen on the
// same task that allocated them.
static void teardown_dma() {
    if (s_adc_handle) {
        adc_continuous_stop(s_adc_handle);
        adc_continuous_deinit(s_adc_handle);
        s_adc_handle = nullptr;
    }
    if (s_read_buf) {
        heap_caps_free(s_read_buf);
        s_read_buf = nullptr;
    }
    // Invalidate captures so bindings know there is no live data.
    portENTER_CRITICAL(&s_capture_mux);
    s_capture_front.valid = false;
    s_capture_back.valid  = false;
    portEXIT_CRITICAL(&s_capture_mux);
    portENTER_CRITICAL(&s_align_mux);
    s_align_front.valid = false;
    s_align_back.valid  = false;
    portEXIT_CRITICAL(&s_align_mux);
}

// Allocate DMA-internal resources and start sampling. Called from the worker
// task in response to a start request. Returns true on success; on failure
// any partial allocations are rolled back via teardown_dma().
static bool startup_dma() {
    // Read buffer — internal RAM for DMA compatibility.
    s_read_buf = (uint8_t*)heap_caps_malloc(SHUTTER_ADC_FRAME_SIZE,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!s_read_buf) {
        LOGE(TAG, "Failed to allocate DMA read buffer (%u B)", SHUTTER_ADC_FRAME_SIZE);
        return false;
    }
    if (!init_adc_continuous()) {
        teardown_dma();
        return false;
    }
    esp_err_t err = adc_continuous_start(s_adc_handle);
    if (err != ESP_OK) {
        LOGE(TAG, "adc_continuous_start failed: %s", esp_err_to_name(err));
        teardown_dma();
        return false;
    }
    return true;
}

// ============================================================================
// Ring Buffer Helpers
// ============================================================================

static inline void ring_push(uint16_t values[SHUTTER_SENSOR_MAX]) {
    for (int i = 0; i < s_active_count; i++) {
        s_ring_buf[i][s_ring_head] = values[i];
    }
    s_ring_head = (s_ring_head + 1) % s_ring_size;
}

// Copy ring buffer contents into the capture buffer's pre-trigger section.
// Returns the number of samples copied.
static uint32_t copy_ring_to_capture(ShutterCapture* cap) {
    uint32_t count = (s_ring_size < SHUTTER_CAPTURE_MAX_SAMPLES) ? s_ring_size : SHUTTER_CAPTURE_MAX_SAMPLES;
    for (int ch = 0; ch < s_active_count; ch++) {
        // Ring head points to the oldest sample. Copy in order: head..end, 0..head-1.
        uint32_t first_part = s_ring_size - s_ring_head;
        if (first_part > count) first_part = count;
        memcpy(cap->sensors[ch].samples, &s_ring_buf[ch][s_ring_head],
               first_part * sizeof(uint16_t));
        if (first_part < count) {
            uint32_t second_part = count - first_part;
            memcpy(&cap->sensors[ch].samples[first_part], s_ring_buf[ch],
                   second_part * sizeof(uint16_t));
        }
    }
    return count;
}

// ============================================================================
// Capture Finalization
// ============================================================================

static void finalize_capture() {
    ShutterCapture* cap = &s_capture_back;
    cap->timestamp_ms  = (uint32_t)millis();
    cap->sensor_count  = s_active_count;
    cap->capture_id    = ++s_capture_id;
    cap->valid         = true;

    for (int ch = 0; ch < s_active_count; ch++) {
        cap->sensors[ch].sample_rate_hz = s_actual_per_sensor_hz;
    }

    // Swap front/back atomically.
    portENTER_CRITICAL(&s_capture_mux);
    ShutterCapture tmp = s_capture_front;
    s_capture_front = s_capture_back;
    s_capture_back = tmp;
    portEXIT_CRITICAL(&s_capture_mux);

    LOGI(TAG, "Capture complete: %lu samples/ch", (unsigned long)s_capture_front.sensors[0].count);
}

// ============================================================================
// Capture Helpers (DRY: shared between CAPTURE_ACTIVE and CAPTURE_COOLDOWN)
// ============================================================================

// Append the latest per-channel samples to the back capture buffer.
// Returns true if any channel's buffer is now full.
static bool append_samples(uint32_t* capture_count, const uint16_t* latest) {
    ShutterCapture* cap = &s_capture_back;
    bool buffer_full = false;
    for (int c = 0; c < s_active_count; c++) {
        if (capture_count[c] < SHUTTER_CAPTURE_MAX_SAMPLES) {
            cap->sensors[c].samples[capture_count[c]++] = latest[c];
        } else {
            buffer_full = true;
        }
    }
    return buffer_full;
}

// Commit the back capture buffer: copy counts, swap front/back, reset state.
// A 200 ms vTaskDelay is applied after finalization to prevent the next trigger
// from starting before the LVGL task has finished reading the capture.
// This is safe because the minimum camera reload time is ~200 ms.
static void commit_capture(uint32_t* capture_count) {
    ShutterCapture* cap = &s_capture_back;
    for (int c = 0; c < s_active_count; c++) {
        cap->sensors[c].count = capture_count[c];
    }
    finalize_capture();
    memset(capture_count, 0, s_active_count * sizeof(uint32_t));
    s_state = CAPTURE_IDLE;
    s_idle_logged = false;
    // Reset background recalibration: the post-capture DMA may briefly burst,
    // and we want only steady-state idle samples in the window. The 100ms drain
    // phase (gated by s_recalib_drain_anchor_us) protects the next window.
    reset_background_recalibration();
    // Yield for one camera reload cycle before accepting the next trigger.
    vTaskDelay(pdMS_TO_TICKS(200));
}

// ============================================================================
// ADC Processing Task
// ============================================================================

static void shutter_adc_task(void* param) {
    (void)param;

    while (true) {
        // Park here until shutter_adc_start() posts the run semaphore.
        // No DMA-internal RAM is held while parked.
        xSemaphoreTake(s_run_sem, portMAX_DELAY);

        // Try to bring the engine up. Outcome is reported back to the caller
        // via s_start_result + s_start_done_sem so failure is observable.
        bool ok = startup_dma();
        s_start_result = ok;
        if (ok) {
            // Reset all per-run accumulators so the new run begins cleanly.
            reset_calibration_accumulators();
            s_state = CAPTURE_CALIBRATING;
            s_ch_map_ready = false;
            memset(s_ch_to_sensor, -1, sizeof(s_ch_to_sensor));
            s_trigger_debounce = 0;
            s_post_silence_count = 0;
            s_post_capture_count = 0;
            s_alignment_requested = false;
            s_alignment_exit_requested = false;
            s_recalibrate_requested = false;
            s_post_calibration_mode = POST_CAL_IDLE;
            LOGI(TAG, "ADC engine started");
        }
        xSemaphoreGive(s_start_done_sem);
        if (!ok) {
            // Stay parked and wait for the next start attempt.
            s_state = CAPTURE_STOPPED;
            continue;
        }

        // Inner read loop — runs until s_stop_requested is observed at a safe
        // boundary (IDLE / ALIGNMENT / CALIBRATING). Mid-capture stop requests
        // are honored only after the active capture finalizes.
        uint32_t capture_count[SHUTTER_SENSOR_MAX] = {};
        bool stop_now = false;

        while (!stop_now) {
            // Honor stop requests only when the state machine is at a clean
            // boundary so we never tear down DMA mid-capture.
            if (s_stop_requested) {
                CaptureState cs = s_state;
                if (cs == CAPTURE_IDLE || cs == CAPTURE_ALIGNMENT || cs == CAPTURE_CALIBRATING) {
                    stop_now = true;
                    break;
                }
            }

            uint32_t bytes_read = 0;
            esp_err_t err = adc_continuous_read(s_adc_handle, s_read_buf, SHUTTER_ADC_FRAME_SIZE,
                                                 &bytes_read, pdMS_TO_TICKS(100));
            if (err == ESP_ERR_TIMEOUT) {
                static uint32_t timeout_count = 0;
                if (++timeout_count % 50 == 1) {
                    LOGW(TAG, "read timeout #%lu", (unsigned long)timeout_count);
                }
                continue;
            }
            if (err != ESP_OK) {
                LOGW(TAG, "adc_continuous_read error: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

        // Parse conversion results (TYPE2 format: 4 bytes each).
        uint32_t num_results = bytes_read / sizeof(adc_digi_output_data_t);
        adc_digi_output_data_t* results = (adc_digi_output_data_t*)s_read_buf;

        // Build the channel reverse map from the first DMA frame that has data.
        // The DMA-reported channel numbers may differ from the logical channel
        // numbers used in the pattern config (observed on ESP32-P4: config 0,1,2
        // yields DMA ch 2,3,4).  We collect all unique DMA channels with non-zero
        // data, sort them numerically, and assign sensor indices in sorted order
        // so the mapping is deterministic regardless of DMA delivery order.
        if (!s_ch_map_ready && num_results > 0) {
            memset(s_ch_to_sensor, -1, sizeof(s_ch_to_sensor));
            // Collect all unique DMA channel numbers that carry real data.
            uint8_t unique_chs[SOC_ADC_MAX_CHANNEL_NUM];
            int found = 0;
            for (uint32_t i = 0; i < num_results; i++) {
                uint8_t ch = results[i].type2.channel;
                if (ch >= SOC_ADC_MAX_CHANNEL_NUM) continue;
                // Check if already collected.
                bool dup = false;
                for (int j = 0; j < found; j++) {
                    if (unique_chs[j] == ch) { dup = true; break; }
                }
                if (dup) continue;
                // Skip channels that only produce zero (DMA padding).
                if (results[i].type2.data == 0) {
                    // Scan ahead to see if this channel ever has non-zero data.
                    bool has_data = false;
                    for (uint32_t k = i + 1; k < num_results; k++) {
                        if (results[k].type2.channel == ch && results[k].type2.data != 0) {
                            has_data = true; break;
                        }
                    }
                    if (!has_data) continue;  // Phantom/padding channel
                }
                unique_chs[found++] = ch;
            }
            if (found >= s_active_count) {
                // Sort ascending so lowest DMA channel corresponds to lowest
                // configured ADC channel.
                for (int a = 0; a < found - 1; a++) {
                    for (int b = a + 1; b < found; b++) {
                        if (unique_chs[b] < unique_chs[a]) {
                            uint8_t tmp = unique_chs[a];
                            unique_chs[a] = unique_chs[b];
                            unique_chs[b] = tmp;
                        }
                    }
                }
                // Build slot-aware mapping.  DMA remapping preserves relative
                // channel order: sorted DMA channels correspond 1:1 to sorted
                // configured channels.  We sort s_active_channels to find which
                // slot owns each sorted position.
                struct { adc_channel_t ch; uint8_t slot; } sorted_cfg[SHUTTER_SENSOR_MAX];
                for (int s = 0; s < s_active_count; s++) {
                    sorted_cfg[s].ch   = s_active_channels[s];
                    sorted_cfg[s].slot = (uint8_t)s;
                }
                for (int a = 0; a < s_active_count - 1; a++) {
                    for (int b = a + 1; b < s_active_count; b++) {
                        if (sorted_cfg[b].ch < sorted_cfg[a].ch) {
                            auto tmp = sorted_cfg[a];
                            sorted_cfg[a] = sorted_cfg[b];
                            sorted_cfg[b] = tmp;
                        }
                    }
                }
                // Map only the first s_active_count sorted channels.
                for (int k = 0; k < s_active_count; k++) {
                    s_ch_to_sensor[unique_chs[k]] = (int8_t)sorted_cfg[k].slot;
                    LOGI(TAG, "DMA ch %d -> slot %d (S%d)", unique_chs[k],
                         sorted_cfg[k].slot, sorted_cfg[k].slot + 1);
                }
                s_ch_map_ready = true;
                LOGI(TAG, "Channel map ready (%d sensor(s) from %d DMA channels)", s_active_count, found);
            } else {
                // Not enough channels found yet — reset and try on next read.
                memset(s_ch_to_sensor, -1, sizeof(s_ch_to_sensor));
                continue;
            }
        }

        // Demux samples by channel.
        uint16_t latest[SHUTTER_SENSOR_MAX] = {};
        bool have_sample[SHUTTER_SENSOR_MAX] = {};

        for (uint32_t i = 0; i < num_results; i++) {
            adc_digi_output_data_t* p = &results[i];
            uint8_t raw_ch = p->type2.channel;
            uint16_t val = p->type2.data;

            // Map ADC channel number to sensor index via reverse lookup.
            if (raw_ch >= SOC_ADC_MAX_CHANNEL_NUM) continue;
            int8_t sensor = s_ch_to_sensor[raw_ch];
            if (sensor < 0) continue;

            // Diagnostic per-channel counter: counts every sensor-0 DMA
            // arrival independently of set completion.  On ESP32-P4 the DMA
            // delivers duplicate per-channel results, so this counter
            // overcounts relative to the true sample rate.  It is logged
            // alongside the set counter to monitor the DMA duplicate ratio.
            if (sensor == 0) {
                if (s_state == CAPTURE_CALIBRATING && s_calib_count_start_us > 0)
                    s_calib_ch0_count++;
                else if (s_state == CAPTURE_IDLE && s_recalib_start_us > 0)
                    s_recalib_ch0_count++;
            }

            latest[sensor] = val;
            have_sample[sensor] = true;

            // When we have a sample for every active channel, process the set.
            bool all = true;
            for (int c = 0; c < s_active_count; c++) {
                if (!have_sample[c]) { all = false; break; }
            }
            if (!all) continue;

            // Reset for next set.
            memset(have_sample, 0, sizeof(have_sample));

            switch (s_state) {
            case CAPTURE_CALIBRATING: {
                // Count one full sample-set per sensor; measure wall-clock to derive actual rate.
                // Also feed the ring buffer so the first capture's pre-trigger lookback contains
                // real baseline samples instead of the 0xFF init pattern.
                ring_push(latest);
                if (s_calib_start_us == 0) {
                    s_calib_start_us = esp_timer_get_time();
                    continue;
                }
                // Wait 100ms wall-clock for the DMA startup backlog to drain. Discarding by
                // sample COUNT does not work: backlogged samples are delivered at wire speed
                // (much faster than real-time) and would still bias the rate high. Only
                // wall-clock time can tell us when the pipeline is in steady state.
                int64_t drain_elapsed_us = esp_timer_get_time() - s_calib_start_us;
                if (drain_elapsed_us < 100000) continue;  // 100 ms
                // After the backlog has drained, anchor the count window.
                if (s_calib_count_start_us == 0) {
                    s_calib_count_start_us = esp_timer_get_time();
                    s_calib_sample_count = 0;
                    s_calib_ch0_count = 0;
                    continue;
                }
                s_calib_sample_count++;
                // Accumulate per-sensor sums for dark baseline computation.
                for (int c = 0; c < s_active_count; c++) {
                    s_baseline_sum[c] += latest[c];
                }
                s_baseline_count++;
                int64_t elapsed_us = esp_timer_get_time() - s_calib_count_start_us;
                if (elapsed_us >= (int64_t)SHUTTER_CALIB_WINDOW_MS * 1000 && s_calib_sample_count > 0) {
                    // Use complete-set count for rate derivation. Each set represents
                    // one true round-robin cycle. The ch0 counter overcounts because
                    // the P4 DMA delivers duplicate per-channel results.
                    s_actual_per_sensor_hz =
                        (float)s_calib_sample_count * 1000000.0f / (float)elapsed_us;
                    LOGI(TAG, "Calibrated sample rate: %.1f Hz/sensor (configured %lu, ratio %.3f, %lu sets / %lu ch0 in %lld ms)",
                         s_actual_per_sensor_hz, (unsigned long)SHUTTER_SAMPLE_RATE_HZ,
                         s_actual_per_sensor_hz / (float)SHUTTER_SAMPLE_RATE_HZ,
                         (unsigned long)s_calib_sample_count, (unsigned long)s_calib_ch0_count,
                         (long long)(elapsed_us / 1000));
                    // Compute per-sensor dark baselines from calibration samples.
                    if (s_baseline_count > 0) {
                        for (int c = 0; c < s_active_count; c++) {
                            s_baseline[c] = (uint16_t)(s_baseline_sum[c] / s_baseline_count);
                        }
                        char baseline_log[160];
                        int pos = 0;
                        for (int c = 0; c < s_active_count && pos < (int)sizeof(baseline_log) - 16; c++) {
                            pos += snprintf(baseline_log + pos, sizeof(baseline_log) - pos,
                                            "S%d=%u ", c + 1, s_baseline[c]);
                        }
                        LOGI(TAG, "Baselines: %s(from %lu samples)",
                             baseline_log, (unsigned long)s_baseline_count);
                        // Derive adaptive trigger threshold from the lowest baseline.
                        // This prevents false triggers when sensors are exposed to
                        // ambient light (idle values drift toward the fixed threshold).
                        uint16_t min_baseline = s_baseline[0];
                        for (int c = 1; c < s_active_count; c++) {
                            if (s_baseline[c] < min_baseline) min_baseline = s_baseline[c];
                        }
                        uint16_t new_thr = (min_baseline > SHUTTER_TRIGGER_MARGIN)
                                         ? (min_baseline - SHUTTER_TRIGGER_MARGIN)
                                         : 0;
                        // Never less sensitive than the fixed default.
                        if (new_thr > SHUTTER_DEFAULT_THRESHOLD) new_thr = SHUTTER_DEFAULT_THRESHOLD;
                        s_threshold = new_thr;
                        LOGI(TAG, "Adaptive trigger threshold: %u (min baseline %u, margin %u)",
                             new_thr, min_baseline, (unsigned)SHUTTER_TRIGGER_MARGIN);
                    }
                    PostCalibrationMode post_cal_mode = s_post_calibration_mode;
                    s_post_calibration_mode = POST_CAL_IDLE;
                    if (post_cal_mode == POST_CAL_ALIGNMENT) {
                        enter_alignment_mode();
                        LOGI(TAG, "Calibration complete, entering deferred alignment mode");
                    } else {
                        s_state = CAPTURE_IDLE;
                    }
                }
                continue;  // Skip trigger logic during calibration
            }
            case CAPTURE_IDLE: {
                if (s_recalibrate_requested) {
                    s_recalibrate_requested = false;
                    s_alignment_requested = false;
                    s_post_calibration_mode = POST_CAL_IDLE;
                    reset_calibration_accumulators();
                    s_state = CAPTURE_CALIBRATING;
                    LOGI(TAG, "Manual recalibration requested");
                    continue;
                }

                // Check for alignment mode entry request (cross-core flag).
                if (s_alignment_requested) {
                    enter_alignment_mode();
                    LOGI(TAG, "Entering alignment mode");
                    break;
                }

                // Feed ring buffer.
                ring_push(latest);

                // Continuous background recalibration: refine sample rate during idle.
                // The window grows without reset for ever-increasing precision; only the
                // sanity-checked rate is published. Resets on every capture transition
                // (commit_capture + trigger) so we only measure steady-state idle.
                // Continuous background recalibration: refine sample rate during idle.
                // Mirror boot-cal pattern: discard the first 100ms of wall-clock samples
                // (post-capture / post-trigger DMA can briefly burst), then anchor a fresh
                // ever-growing window. Sanity-checked rate is published every 5s, 10s, ...
                // Resets in commit_capture and at trigger so we only measure clean idle.
                if (s_recalib_drain_anchor_us == 0) {
                    s_recalib_drain_anchor_us = esp_timer_get_time();
                } else if (s_recalib_start_us == 0) {
                    if (esp_timer_get_time() - s_recalib_drain_anchor_us >= RECALIB_DRAIN_US) {
                        s_recalib_start_us = esp_timer_get_time();
                        s_recalib_sample_count = 0;
                        s_recalib_ch0_count = 0;
                        s_recalib_next_update_us = RECALIB_UPDATE_INTERVAL_US;
                    }
                } else {
                    s_recalib_sample_count++;
                    int64_t recalib_elapsed_us = esp_timer_get_time() - s_recalib_start_us;
                    if (recalib_elapsed_us >= s_recalib_next_update_us &&
                        s_recalib_sample_count > 1000) {
                        float new_rate = (float)s_recalib_sample_count * 1000000.0f /
                                         (float)recalib_elapsed_us;
                        // First recalibration accepts unconditionally — the boot
                        // calibration can be significantly off when DMA is still
                        // settling. Subsequent updates use 5% sanity window.
                        bool accept = s_recalib_first ||
                                      (new_rate > s_actual_per_sensor_hz * 0.95f &&
                                       new_rate < s_actual_per_sensor_hz * 1.05f);
                        if (accept) {
                            s_actual_per_sensor_hz = new_rate;
                            LOGI(TAG, "Recalibrated: %.1f Hz/sensor (%lu sets / %lu ch0 in %lld ms%s)",
                                 new_rate, (unsigned long)s_recalib_sample_count,
                                 (unsigned long)s_recalib_ch0_count,
                                 (long long)(recalib_elapsed_us / 1000),
                                 s_recalib_first ? ", first" : "");
                            s_recalib_first = false;
                        }
                        // Schedule the next update one interval further out, but never
                        // reset the window itself — it keeps growing for ever-increasing
                        // precision (5s, 10s, 15s, ...).
                        s_recalib_next_update_us += RECALIB_UPDATE_INTERVAL_US;
                    }
                }

                // Log idle ADC values once after boot, then only after each capture.
                if (!s_idle_logged) {
                    char idle_log[128];
                    int pos = 0;
                    for (int c = 0; c < s_active_count && pos < (int)sizeof(idle_log) - 16; c++) {
                        pos += snprintf(idle_log + pos, sizeof(idle_log) - pos,
                                        "S%d=%d ", c + 1, latest[c]);
                    }
                    LOGI(TAG, "idle %s(thr=%d)", idle_log, s_threshold);
                    s_idle_logged = true;
                }

                // Check trigger: any active channel below threshold.
                bool any_below = false;
                for (int c = 0; c < s_active_count; c++) {
                    if (latest[c] < s_threshold) { any_below = true; break; }
                }
                if (any_below) {
                    s_trigger_debounce++;
                } else {
                    s_trigger_debounce = 0;
                }
                if (s_trigger_debounce >= SHUTTER_TRIGGER_DEBOUNCE) {
                    s_trigger_debounce = 0;
                    // Copy pre-trigger from ring buffer into back capture.
                    ShutterCapture* cap = &s_capture_back;
                    uint32_t pre_count = copy_ring_to_capture(cap);
                    for (int c = 0; c < s_active_count; c++) {
                        capture_count[c] = pre_count;
                        cap->sensors[c].trigger_index = pre_count;
                    }
                    // Append the triggering sample.
                    for (int c = 0; c < s_active_count; c++) {
                        if (capture_count[c] < SHUTTER_CAPTURE_MAX_SAMPLES) {
                            cap->sensors[c].samples[capture_count[c]++] = latest[c];
                        }
                    }
                    s_state = CAPTURE_ACTIVE;
                    s_post_silence_count = 0;
                    // Reset background recalibration so we never measure across the
                    // idle→active transition (the trigger sample itself is non-idle).
                    reset_background_recalibration();
                }
                break;
            }
            case CAPTURE_ACTIVE: {
                // Abort capture if alignment was requested (user action overrides stray trigger).
                if (s_alignment_requested) {
                    memset(capture_count, 0, s_active_count * sizeof(uint32_t));
                    enter_alignment_mode();
                    LOGI(TAG, "Aborted active capture, entering alignment mode");
                    break;
                }
                bool buffer_full = append_samples(capture_count, latest);

                // Check if pulse ended (all active channels back above threshold).
                bool all_above = true;
                for (int c = 0; c < s_active_count; c++) {
                    if (latest[c] < s_threshold) { all_above = false; break; }
                }
                if (all_above) {
                    s_post_silence_count++;
                    if (s_post_silence_count >= SHUTTER_POST_TRIGGER_SAMPLES) {
                        // Pulse complete — transition to cooldown for post-capture context.
                        if (SHUTTER_POST_CAPTURE_SAMPLES > 0) {
                            s_post_capture_count = 0;
                            s_state = CAPTURE_COOLDOWN;
                        } else {
                            commit_capture(capture_count);
                        }
                    }
                } else {
                    s_post_silence_count = 0;
                }

                if (buffer_full) {
                    commit_capture(capture_count);
                }
                break;
            }
            case CAPTURE_COOLDOWN: {
                // Abort capture if alignment was requested (user action overrides stray trigger).
                if (s_alignment_requested) {
                    memset(capture_count, 0, s_active_count * sizeof(uint32_t));
                    enter_alignment_mode();
                    LOGI(TAG, "Aborted cooldown capture, entering alignment mode");
                    break;
                }
                // Post-capture extension: keep recording baseline after pulse ends.
                bool buffer_full = append_samples(capture_count, latest);
                s_post_capture_count++;
                if (s_post_capture_count >= SHUTTER_POST_CAPTURE_SAMPLES || buffer_full) {
                    commit_capture(capture_count);
                }
                break;
            }
            case CAPTURE_ALIGNMENT: {
                if (s_recalibrate_requested) {
                    s_recalibrate_requested = false;
                    s_post_calibration_mode = s_alignment_exit_requested ? POST_CAL_IDLE : POST_CAL_ALIGNMENT;
                    s_alignment_exit_requested = false;
                    reset_calibration_accumulators();
                    s_state = CAPTURE_CALIBRATING;
                    LOGI(TAG, "Recalibration requested from alignment mode");
                    continue;
                }

                // Check for exit request (cross-core flag).
                if (s_alignment_exit_requested) {
                    s_alignment_exit_requested = false;
                    // Invalidate front buffer so readers see alignment inactive.
                    portENTER_CRITICAL(&s_align_mux);
                    s_align_front.valid = false;
                    portEXIT_CRITICAL(&s_align_mux);
                    s_state = CAPTURE_IDLE;
                    s_idle_logged = false;
                    // Reset recalibration — samples during alignment were not dark.
                    reset_background_recalibration();
                    LOGI(TAG, "Exiting alignment mode");
                    break;
                }

                // Accumulate samples for decimation.
                for (int c = 0; c < s_active_count; c++) {
                    s_align_accum[c] += latest[c];
                }
                s_align_accum_count++;

                if (s_align_accum_count >= SHUTTER_ALIGN_DECIMATION_SAMPLES) {
                    ShutterAlignmentReading* r = &s_align_back;
                    r->sensor_count = s_active_count;
                    r->timestamp_ms = (uint32_t)millis();

                    // Compute per-sensor averaged raw and percentage.
                    uint8_t max_pct = 0;
                    uint8_t min_pct = 100;
                    for (int c = 0; c < s_active_count; c++) {
                        uint16_t avg_adc = (uint16_t)(s_align_accum[c] / s_align_accum_count);
                        r->raw[c] = avg_adc;

                        // pct = 100 * (baseline - adc) / baseline, clamped 0-100.
                        if (s_baseline[c] > 0 && avg_adc < s_baseline[c]) {
                            uint32_t pct32 = (uint32_t)100 * (s_baseline[c] - avg_adc) / s_baseline[c];
                            r->pct[c] = (uint8_t)(pct32 > 100 ? 100 : pct32);
                        } else {
                            r->pct[c] = 0;
                        }
                        if (r->pct[c] > max_pct) max_pct = r->pct[c];
                        if (r->pct[c] < min_pct) min_pct = r->pct[c];
                    }
                    // Zero out unused sensor slots.
                    for (int c = s_active_count; c < SHUTTER_SENSOR_MAX; c++) {
                        r->raw[c] = 0;
                        r->pct[c] = 0;
                    }

                    r->spread_pct = (uint16_t)(max_pct - min_pct);

                    // Compute 3-tier status + hint (priority-ordered rules).
                    bool all_below_10_pct = true;
                    bool any_above_95 = false, any_above_92 = false;
                    bool any_below_40 = false, any_below_60 = false;
                    bool all_80_92 = true;
                    for (int c = 0; c < s_active_count; c++) {
                        uint8_t p = r->pct[c];
                        if (p >= 10) all_below_10_pct = false;
                        if (p > 95) any_above_95 = true;
                        if (p > 92) any_above_92 = true;
                        if (p < 40) any_below_40 = true;
                        if (p < 60) any_below_60 = true;
                        if (p < 80 || p > 92) all_80_92 = false;
                    }

                    if (all_below_10_pct)                      { r->status = "not-ready"; r->hint = "No light detected"; }
                    else if (any_above_95)                     { r->status = "not-ready"; r->hint = "Too bright - clipping"; }
                    else if (any_above_92)                     { r->status = "usable";    r->hint = "Too bright"; }
                    else if (any_below_40)                     { r->status = "not-ready"; r->hint = "Too dim"; }
                    else if (any_below_60)                     { r->status = "usable";    r->hint = "Light is low"; }
                    else if (r->spread_pct > 20)               { r->status = "not-ready"; r->hint = "Uneven lighting"; }
                    else if (r->spread_pct > 10)               { r->status = "usable";    r->hint = "Slightly uneven"; }
                    else if (all_80_92 && r->spread_pct <= 10) { r->status = "ready";     r->hint = ""; }
                    else                                       { r->status = "usable";    r->hint = "Aim for 80-92%"; }

                    r->valid = true;

                    // Swap front/back atomically.
                    portENTER_CRITICAL(&s_align_mux);
                    ShutterAlignmentReading tmp = s_align_front;
                    s_align_front = s_align_back;
                    s_align_back = tmp;
                    portEXIT_CRITICAL(&s_align_mux);

                    // Reset accumulators.
                    memset(s_align_accum, 0, sizeof(s_align_accum));
                    s_align_accum_count = 0;
                }
                break;
            }
            default:
                s_state = CAPTURE_IDLE;
                break;
            }
        }

        // Yield periodically to avoid watchdog.
        taskYIELD();
        } // end inner read loop (while !stop_now)

        // Tear down DMA-internal resources on this task. The caller of
        // shutter_adc_stop() is blocked on s_parked_sem and will resume after
        // we post it. Reset request flag last so a fresh start() sees a clean
        // slate.
        teardown_dma();
        s_state = CAPTURE_STOPPED;
        s_stop_requested = false;
        LOGI(TAG, "ADC engine stopped (parked)");
        xSemaphoreGive(s_parked_sem);
    } // end outer park loop (while true)
}

// ============================================================================
// Public API
// ============================================================================

void shutter_adc_set_slot_mapping(const ShutterSensorSlotMapping* mapping) {
    s_has_slot_mapping = false;
    memset(s_active_channels, 0, sizeof(s_active_channels));

    if (!mapping || mapping->count == 0) return;

    uint8_t count = mapping->count;
    if (count > SHUTTER_SENSOR_MAX) count = SHUTTER_SENSOR_MAX;

    for (int slot = 0; slot < count; slot++) {
        int gpio = mapping->gpio_pins[slot];
        // Find this GPIO in the known pin table to get its ADC channel.
        bool found = false;
        for (int k = 0; k < SHUTTER_SENSOR_MAX; k++) {
            if (s_gpio_pins[k] == gpio && gpio >= 0) {
                s_active_channels[slot] = s_all_channels[k];
                found = true;
                break;
            }
        }
        if (!found) {
            LOGE(TAG, "Slot %d: GPIO %d not in known pin table", slot, gpio);
            memset(s_active_channels, 0, sizeof(s_active_channels));
            return;
        }
    }

    s_has_slot_mapping = true;
    LOGI(TAG, "Slot mapping set: %d slot(s)", count);
}

void shutter_adc_init(uint8_t active_sensor_count) {
    s_active_count = active_sensor_count;
    if (s_active_count == 0 || s_active_count > SHUTTER_SENSOR_MAX) {
        LOGE(TAG, "Invalid active_sensor_count %d", s_active_count);
        return;
    }

    // Apply default identity mapping if no slot mapping was set.
    if (!s_has_slot_mapping) {
        for (int i = 0; i < s_active_count; i++) {
            s_active_channels[i] = s_all_channels[i];
        }
    }
    if (!allocate_buffers()) {
        LOGE(TAG, "Buffer allocation failed — shutter tester disabled");
        return;
    }

    s_capture_front.valid = false;
    s_capture_back.valid  = false;
    s_capture_id          = 0;

    // Lifecycle synchronization primitives. The task will block on s_run_sem
    // immediately after creation and stay parked until shutter_adc_start()
    // posts the semaphore. This pattern keeps the ~28 KB of DMA-internal RAM
    // unallocated whenever no feature is using the engine.
    s_run_sem        = xSemaphoreCreateBinary();
    s_start_done_sem = xSemaphoreCreateBinary();
    s_parked_sem     = xSemaphoreCreateBinary();
    s_lifecycle_mux  = xSemaphoreCreateMutex();
    if (!s_run_sem || !s_start_done_sem || !s_parked_sem || !s_lifecycle_mux) {
        LOGE(TAG, "Failed to create lifecycle semaphores");
        return;
    }
    s_state          = CAPTURE_STOPPED;
    s_stop_requested = false;

    // Create capture task on APP core (core 1) with generous stack for DMA processing.
    // Task begins by blocking on s_run_sem (parked); no DMA-internal RAM is
    // held until a feature calls shutter_capture_acquire().
    BaseType_t ret = xTaskCreatePinnedToCore(
        shutter_adc_task, "shutter_adc", 8192, nullptr, 5,
        &s_task_handle, 1);
    if (ret != pdPASS) {
        LOGE(TAG, "Failed to create ADC task");
        return;
    }

    s_available = true;
    // Build a compact GPIO list for the log line (slot-ordered).
    char gpio_log[64]; int gpos = 0;
    for (int i = 0; i < s_active_count && gpos < (int)sizeof(gpio_log) - 8; i++) {
        // Find the GPIO for this slot's ADC channel.
        int gpio = -1;
        for (int k = 0; k < SHUTTER_SENSOR_MAX; k++) {
            if (s_all_channels[k] == s_active_channels[i] && s_gpio_pins[k] >= 0) {
                gpio = s_gpio_pins[k];
                break;
            }
        }
        gpos += snprintf(gpio_log + gpos, sizeof(gpio_log) - gpos, "GPIO%d ", gpio);
    }
    LOGI(TAG, "Shutter ADC configured (parked): %d sensor(s) on %s%s", s_active_count, gpio_log,
         s_has_slot_mapping ? "(slot-mapped)" : "");
}

bool shutter_adc_start() {
    if (!s_available) {
        LOGW(TAG, "start ignored: engine not configured");
        return false;
    }
    xSemaphoreTake(s_lifecycle_mux, portMAX_DELAY);

    // Idempotent: if the task is already running, return success without
    // touching the semaphores.
    if (s_state != CAPTURE_STOPPED) {
        xSemaphoreGive(s_lifecycle_mux);
        return true;
    }

    // Drain any stale post on s_start_done_sem from a previous attempt.
    xSemaphoreTake(s_start_done_sem, 0);
    s_start_result = false;
    xSemaphoreGive(s_run_sem);

    // Wait for the worker to either complete startup_dma() or fail. The
    // worker always posts s_start_done_sem exactly once per start attempt.
    xSemaphoreTake(s_start_done_sem, portMAX_DELAY);
    bool ok = s_start_result;
    xSemaphoreGive(s_lifecycle_mux);
    return ok;
}

void shutter_adc_stop() {
    if (!s_available) return;
    xSemaphoreTake(s_lifecycle_mux, portMAX_DELAY);

    if (s_state == CAPTURE_STOPPED) {
        xSemaphoreGive(s_lifecycle_mux);
        return;
    }

    // Drain any stale park signal from a previous teardown cycle.
    xSemaphoreTake(s_parked_sem, 0);
    s_stop_requested = true;

    // Worker honors the request at a safe boundary (IDLE / ALIGNMENT /
    // CALIBRATING) so in-flight captures are never torn down mid-trigger.
    // commit_capture() may add ~200 ms of camera reload delay; the
    // semaphore wait below covers that.
    xSemaphoreTake(s_parked_sem, portMAX_DELAY);
    xSemaphoreGive(s_lifecycle_mux);
}

bool shutter_adc_is_running() {
    if (!s_available) return false;
    return s_state != CAPTURE_STOPPED;
}

bool shutter_adc_get_capture(ShutterCapture* out) {
    if (!s_available || !out) return false;

    portENTER_CRITICAL(&s_capture_mux);
    bool valid = s_capture_front.valid;
    if (valid) {
        out->capture_id   = s_capture_front.capture_id;
        out->timestamp_ms = s_capture_front.timestamp_ms;
        out->sensor_count = s_capture_front.sensor_count;
        out->valid        = true;
        for (int i = 0; i < s_active_count; i++) {
            out->sensors[i].count         = s_capture_front.sensors[i].count;
            out->sensors[i].trigger_index = s_capture_front.sensors[i].trigger_index;
            out->sensors[i].sample_rate_hz = s_capture_front.sensors[i].sample_rate_hz;
            out->sensors[i].samples       = s_capture_front.sensors[i].samples;
        }
    }
    portEXIT_CRITICAL(&s_capture_mux);
    return valid;
}

uint16_t shutter_adc_get_threshold() {
    return s_threshold;
}

void shutter_adc_set_threshold(uint16_t threshold) {
    portENTER_CRITICAL(&s_capture_mux);
    s_threshold = threshold;
    portEXIT_CRITICAL(&s_capture_mux);
}

bool shutter_adc_is_available() {
    return s_available;
}

float shutter_adc_get_sample_rate_hz() {
    return s_actual_per_sensor_hz;
}

// ============================================================================
// Alignment Mode API (internal — called by shutter_capture.cpp)
// ============================================================================

void shutter_adc_start_alignment() {
    if (s_state == CAPTURE_ALIGNMENT) return;  // Already in alignment (idempotent)
    s_recalibrate_requested = false;
    if (s_state == CAPTURE_CALIBRATING) {
        s_post_calibration_mode = POST_CAL_ALIGNMENT;
        return;
    }
    s_alignment_requested = true;
}

void shutter_adc_stop_alignment() {
    s_alignment_requested = false;
    if (s_state == CAPTURE_CALIBRATING) {
        s_post_calibration_mode = POST_CAL_IDLE;
        return;
    }
    if (s_state != CAPTURE_ALIGNMENT) return;  // Not in alignment (idempotent)
    s_alignment_exit_requested = true;
}

bool shutter_adc_is_alignment_active() {
    return s_state == CAPTURE_ALIGNMENT;
}

void shutter_adc_recalibrate() {
    if (s_state == CAPTURE_ACTIVE || s_state == CAPTURE_COOLDOWN) {
        LOGW(TAG, "Cannot recalibrate: capture in progress");
        return;
    }
    if (s_state == CAPTURE_ALIGNMENT) {
        s_post_calibration_mode = POST_CAL_ALIGNMENT;
    } else if (s_state != CAPTURE_CALIBRATING) {
        s_post_calibration_mode = POST_CAL_IDLE;
    }
    s_alignment_requested = false;
    s_recalibrate_requested = true;
}

bool shutter_adc_is_calibrating() {
    return s_state == CAPTURE_CALIBRATING;
}

bool shutter_adc_get_alignment(ShutterAlignmentReading* out) {
    if (!out || s_state != CAPTURE_ALIGNMENT) return false;
    portENTER_CRITICAL(&s_align_mux);
    bool valid = s_align_front.valid;
    if (valid) *out = s_align_front;
    portEXIT_CRITICAL(&s_align_mux);
    return valid;
}

#endif // IS_SHUTTER_TESTER

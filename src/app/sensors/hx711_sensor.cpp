#include "hx711_sensor.h"

#if HAS_SENSOR_HX711

#include "log_manager.h"
#include "config_manager.h"
#include "sensor_manager.h"
#if HAS_DISPLAY
#include "brew_manager.h"
#endif
#include <HX711.h>

#define TAG "HX711"

// ============================================================================
// Tuning knobs — adjust these to taste
// ============================================================================

// Weight EMA filter
static constexpr float    EMA_ALPHA          = 0.3f;   // 0→stable, 1→raw. 0.3 = 30% new + 70% history
static constexpr float    JUMP_THRESHOLD     = 5.0f;   // grams — reset EMA instantly on big change

// Flow rate sliding window (time-based — works at any sample rate)
static constexpr size_t   FLOW_RING_CAPACITY = 80;     // ring buffer capacity (fits 1s@80SPS or 8s@10SPS)
static constexpr uint32_t FLOW_WINDOW_MS     = 500;    // lookback window for derivative (ms)
static constexpr uint32_t FLOW_UPDATE_MS     = 100;    // recalc interval (100ms → 10 Hz updates)
static constexpr uint32_t FLOW_MIN_SPAN_MS   = 50;     // minimum span for valid derivative (ms)

// ============================================================================
// State
// ============================================================================
static HX711 s_scale;
static bool  s_initialized = false;
static bool  s_available   = false;

// EMA-filtered weight
static float s_weight_ema  = 0.0f;
static bool  s_ema_primed  = false;

// Flow rate — sliding window ring buffer
struct WeightSample { float weight; uint32_t ms; };
static WeightSample s_flow_ring[FLOW_RING_CAPACITY];
static size_t   s_ring_head     = 0;      // next write index
static size_t   s_ring_count    = 0;      // samples currently stored
static uint32_t s_flow_last_ms  = 0;      // last time we recalculated
static float    s_flow_rate     = 0.0f;   // g/s

// Calibration reference weight (runtime, not persisted)
static float s_cal_weight = 500.0f;

// Deferred NVS persist flag (set from any task, consumed in loop_cb on main task)
static volatile bool s_persist_requested = false;

// Deferred operation flags (set from LVGL task, consumed on main task)
static volatile bool s_tare_requested      = false;
static volatile bool s_tare_persist        = true;
static volatile bool s_calibrate_requested = false;

// Status tracking
enum ScaleStatus : uint8_t { SCALE_IDLE = 0, SCALE_TARING, SCALE_CALIBRATING };
static volatile ScaleStatus s_status = SCALE_IDLE;

// ============================================================================
// Internal helpers
// ============================================================================

// Read one sample from HX711, apply EMA, update flow rate.
static void poll_once() {
    if (!s_available) return;
    if (!s_scale.is_ready()) return;

    float raw = s_scale.get_units(1);  // single sample, calibrated

    // EMA filter with jump detection
    if (!s_ema_primed) {
        s_weight_ema = raw;
        s_ema_primed = true;
    } else {
        float delta = raw - s_weight_ema;
        if (delta > JUMP_THRESHOLD || delta < -JUMP_THRESHOLD) {
            s_weight_ema = raw;  // instant reset on big change
        } else {
            s_weight_ema = EMA_ALPHA * raw + (1.0f - EMA_ALPHA) * s_weight_ema;
        }
    }

    // Push sample into ring buffer
    uint32_t now = millis();
    s_flow_ring[s_ring_head] = { s_weight_ema, now };
    s_ring_head = (s_ring_head + 1) % FLOW_RING_CAPACITY;
    if (s_ring_count < FLOW_RING_CAPACITY) s_ring_count++;

    // Recalculate flow rate at FLOW_UPDATE_MS intervals
    if (s_ring_count >= 2 && (now - s_flow_last_ms) >= FLOW_UPDATE_MS) {
        s_flow_last_ms = now;
        size_t newest_idx = (s_ring_head + FLOW_RING_CAPACITY - 1) % FLOW_RING_CAPACITY;
        const WeightSample &newest = s_flow_ring[newest_idx];

        // Find reference sample ~FLOW_WINDOW_MS ago (scan backward from newest)
        uint32_t target_ms = now - FLOW_WINDOW_MS;
        size_t ref_idx = (s_ring_head + FLOW_RING_CAPACITY - s_ring_count) % FLOW_RING_CAPACITY;
        for (size_t i = 1; i < s_ring_count; i++) {
            size_t idx = (s_ring_head + FLOW_RING_CAPACITY - 1 - i) % FLOW_RING_CAPACITY;
            if (s_flow_ring[idx].ms <= target_ms) {
                ref_idx = idx;
                break;
            }
        }

        const WeightSample &ref = s_flow_ring[ref_idx];
        uint32_t span = newest.ms - ref.ms;
        if (span >= FLOW_MIN_SPAN_MS) {
            s_flow_rate = (newest.weight - ref.weight) / ((float)span / 1000.0f);
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

float hx711_get_weight() {
    return s_weight_ema;
}

float hx711_get_flow_rate() {
    return s_flow_rate;
}

bool hx711_is_available() {
    return s_available;
}

void hx711_tare() {
    if (!s_available) return;
    LOGI(TAG, "Tare (20 samples)...");
    s_scale.tare(20);
    s_weight_ema  = 0.0f;
    s_ema_primed  = false;
    s_flow_rate   = 0.0f;
    s_ring_head   = 0;
    s_ring_count  = 0;
    s_flow_last_ms = 0;
    LOGI(TAG, "Tare done, offset=%ld", s_scale.get_offset());
}

void hx711_set_calibration(float factor) {
    if (!s_available) return;
    s_scale.set_scale(factor);
    LOGI(TAG, "Calibration factor set: %.4f", factor);
}

float hx711_get_calibration_factor() {
    if (!s_available) return 1.0f;
    return s_scale.get_scale();
}

long hx711_get_offset() {
    if (!s_available) return 0;
    return s_scale.get_offset();
}

float hx711_get_value(int times) {
    if (!s_available) return 0.0f;
    return (float)s_scale.get_value(times);
}

float hx711_get_cal_weight() {
    return s_cal_weight;
}

void hx711_adjust_cal_weight(float delta) {
    s_cal_weight += delta;
    if (s_cal_weight < 1.0f) s_cal_weight = 1.0f;
    LOGI(TAG, "Cal weight adjusted by %.1f → %.1f g", delta, s_cal_weight);
}

void hx711_set_cal_weight(float value) {
    s_cal_weight = (value < 1.0f) ? 1.0f : value;
    LOGI(TAG, "Cal weight set to %.1f g", s_cal_weight);
}

float hx711_calibrate_with_cal_weight() {
    if (!s_available) return 0.0f;
    float raw_delta = hx711_get_value(20);
    if (raw_delta == 0.0f) {
        LOGW(TAG, "Calibrate failed: raw delta is zero");
        return 0.0f;
    }
    float factor = raw_delta / s_cal_weight;
    LOGI(TAG, "Calibrate: raw_delta=%.1f / cal_weight=%.1f → factor=%.4f", raw_delta, s_cal_weight, factor);
    hx711_set_calibration(factor);
    return factor;
}

void hx711_request_persist() {
    s_persist_requested = true;
}

void hx711_request_tare() {
    s_tare_requested = true;
    s_tare_persist   = true;
    s_status = SCALE_TARING;
}

void hx711_request_tare_no_persist() {
    s_tare_requested = true;
    s_tare_persist   = false;
    s_status = SCALE_TARING;
}

void hx711_request_calibrate() {
    s_calibrate_requested = true;
    s_status = SCALE_CALIBRATING;
}

const char* hx711_get_status() {
    switch (s_status) {
        case SCALE_TARING:      return "taring";
        case SCALE_CALIBRATING: return "calibrating";
        default:                return "idle";
    }
}

// ============================================================================
// Sensor callback wiring
// ============================================================================

static void hx711_init_cb() {
    if (s_initialized) return;
    s_initialized = true;

    if (HX711_DOUT_PIN < 0 || HX711_SCK_PIN < 0) {
        LOGW(TAG, "HX711 pins not configured (DOUT=%d SCK=%d)", HX711_DOUT_PIN, HX711_SCK_PIN);
        return;
    }

    s_scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);

    if (!s_scale.wait_ready_timeout(1000)) {
        LOGE(TAG, "HX711 not detected (timeout)");
        return;
    }

    // Load calibration from NVS via device_config
    extern DeviceConfig device_config;
    float cal = strtof(device_config.hx711_cal_factor, nullptr);
    long  ofs = strtol(device_config.hx711_offset, nullptr, 10);
    if (cal == 0.0f) cal = 1.0f;

    s_scale.set_scale(cal);
    s_scale.set_offset(ofs);

    s_available = true;
    LOGI(TAG, "HX711 ready (DOUT=%d SCK=%d cal=%.4f ofs=%ld)",
         HX711_DOUT_PIN, HX711_SCK_PIN, cal, ofs);
}

static void hx711_loop_cb() {
    // Deferred tare — runs on main task (internal RAM stack, no LVGL blocking)
    if (s_tare_requested && s_available) {
        s_tare_requested = false;
        bool persist = s_tare_persist;
        s_tare_persist = true;  // reset default
        hx711_tare();
        if (persist) s_persist_requested = true;
        s_status = SCALE_IDLE;
    }

    // Deferred calibrate — runs on main task
    if (s_calibrate_requested && s_available) {
        s_calibrate_requested = false;
        float factor = hx711_calibrate_with_cal_weight();
        if (factor != 0.0f) {
            s_persist_requested = true;
            LOGI(TAG, "Deferred calibrate done: factor=%.4f", factor);
        } else {
            LOGW(TAG, "Deferred calibrate failed (zero raw delta)");
        }
        s_status = SCALE_IDLE;
    }

    poll_once();

#if HAS_DISPLAY
    // Tick the brew state machine after each weight sample
    brew_tick();
#endif

    // Deferred NVS persist — runs on main task (internal RAM stack, flash-safe)
    if (s_persist_requested && s_available) {
        s_persist_requested = false;
        extern DeviceConfig device_config;
        snprintf(device_config.hx711_cal_factor, CONFIG_HX711_CAL_MAX_LEN, "%.4f", hx711_get_calibration_factor());
        snprintf(device_config.hx711_offset, CONFIG_HX711_CAL_MAX_LEN, "%ld", hx711_get_offset());
        config_manager_save(&device_config);
        LOGI(TAG, "Calibration persisted to NVS (factor=%.4f offset=%ld)",
             hx711_get_calibration_factor(), hx711_get_offset());
    }
}

static void hx711_append_api(JsonObject &doc) {
    if (!s_available) return;
    sensor_manager_set_number(doc, "weight_g", s_weight_ema, true);
    sensor_manager_set_number(doc, "flow_rate", s_flow_rate, true);
}

static void hx711_append_mqtt(JsonObject &doc) {
    hx711_append_api(doc);
}

void register_hx711_sensor(SensorRegistry &registry) {
    SensorCallbacks callbacks = {};
    callbacks.name       = "HX711";
    callbacks.init       = hx711_init_cb;
    callbacks.loop       = hx711_loop_cb;
    callbacks.append_api = hx711_append_api;
    callbacks.append_mqtt = hx711_append_mqtt;
    registry.add(callbacks);
}

#endif // HAS_SENSOR_HX711

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

#include "scale_smoothing.h"

// ============================================================================
// State
// ============================================================================
static HX711 s_scale;
static bool  s_initialized = false;
static bool  s_available   = false;
static ScaleSmoothingState s_smooth_state = {};

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
    scale_smoothing_process(s_smooth_state, raw, millis());
}

// ============================================================================
// Public API
// ============================================================================

float hx711_get_weight() {
    return s_smooth_state.weight_display;
}

float hx711_get_weight_ema() {
    return s_smooth_state.weight_ema;
}

float hx711_get_flow_rate() {
    return s_smooth_state.flow_rate;
}

bool hx711_is_available() {
    return s_available;
}

void hx711_tare() {
    if (!s_available) return;
    LOGI(TAG, "Tare (20 samples)...");
    s_scale.tare(20);
    scale_smoothing_reset(s_smooth_state);
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
    float cal = strtof(device_config.scale_cal_factor, nullptr);
    long  ofs = strtol(device_config.scale_offset, nullptr, 10);
    if (cal == 0.0f) cal = 1.0f;

    s_scale.set_scale(cal);
    s_scale.set_offset(ofs);

    s_available = true;
    LOGI(TAG, "HX711 ready (DOUT=%d SCK=%d cal=%.4f ofs=%ld)",
         HX711_DOUT_PIN, HX711_SCK_PIN, cal, ofs);

    // Apply smoothing preset from config
    scale_smoothing_apply(device_config.scale_smoothing);
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
        snprintf(device_config.scale_cal_factor, CONFIG_SCALE_CAL_MAX_LEN, "%.4f", hx711_get_calibration_factor());
        snprintf(device_config.scale_offset, CONFIG_SCALE_CAL_MAX_LEN, "%ld", hx711_get_offset());
        config_manager_save(&device_config);
        LOGI(TAG, "Calibration persisted to NVS (factor=%.4f offset=%ld)",
             hx711_get_calibration_factor(), hx711_get_offset());
    }
}

void hx711_apply_preset(uint8_t idx) {
    scale_smoothing_apply(idx);
}

static void hx711_append_api(JsonObject &doc) {
    if (!s_available) return;
    sensor_manager_set_number(doc, "weight_g", s_smooth_state.weight_display, true);
    sensor_manager_set_number(doc, "flow_rate", s_smooth_state.flow_rate, true);
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

#include "nau7802_sensor.h"

#if HAS_SENSOR_NAU7802

#include "log_manager.h"
#include "config_manager.h"
#include "sensor_manager.h"
#if HAS_DISPLAY
#include "brew_manager.h"
#endif
#include <SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h>
#include <Wire.h>

#define TAG "NAU7802"

#include "scale_smoothing.h"

// ============================================================================
// State
// ============================================================================
static NAU7802 s_nau;
static bool  s_initialized = false;
static bool  s_available   = false;
static ScaleSmoothingState s_smooth_state = {};

// Calibration
static float s_cal_factor  = 1.0f;
static long  s_offset      = 0;

// Calibration reference weight (runtime, not persisted)
static float s_cal_weight = 500.0f;

// Deferred flags
static volatile bool s_persist_requested   = false;
static volatile bool s_tare_requested      = false;
static volatile bool s_tare_persist        = true;
static volatile bool s_calibrate_requested = false;

enum ScaleStatus : uint8_t { SCALE_IDLE = 0, SCALE_TARING, SCALE_CALIBRATING };
static volatile ScaleStatus s_status = SCALE_IDLE;

// ============================================================================
// Internal helpers
// ============================================================================

static void poll_once() {
    if (!s_available) return;
    if (!s_nau.available()) return;

    long raw = s_nau.getReading();
    float calibrated = (s_cal_factor != 0.0f) ? (float)(raw - s_offset) / s_cal_factor : 0.0f;
    scale_smoothing_process(s_smooth_state, calibrated, millis());
}

// ============================================================================
// Public API
// ============================================================================

float nau7802_get_weight() { return s_smooth_state.weight_display; }
float nau7802_get_weight_ema() { return s_smooth_state.weight_ema; }
float nau7802_get_flow_rate() { return s_smooth_state.flow_rate; }
bool  nau7802_is_available() { return s_available; }

void nau7802_tare() {
    if (!s_available) return;
    LOGI(TAG, "Tare (20 samples)...");
    // Average 20 raw readings for offset
    long total = 0;
    int count = 0;
    for (int i = 0; i < 20; i++) {
        // Wait for data ready
        uint32_t t0 = millis();
        while (!s_nau.available()) {
            if (millis() - t0 > 200) break;
            delay(1);
        }
        if (s_nau.available()) {
            total += s_nau.getReading();
            count++;
        }
    }
    if (count > 0) s_offset = total / count;
    scale_smoothing_reset(s_smooth_state);
    LOGI(TAG, "Tare done, offset=%ld", s_offset);
}

void nau7802_set_calibration(float factor) {
    s_cal_factor = factor;
    LOGI(TAG, "Calibration factor set: %.4f", factor);
}

float nau7802_get_calibration_factor() { return s_cal_factor; }
long  nau7802_get_offset() { return s_offset; }

float nau7802_get_value(int times) {
    if (!s_available) return 0.0f;
    long total = 0;
    int count = 0;
    for (int i = 0; i < times; i++) {
        uint32_t t0 = millis();
        while (!s_nau.available()) {
            if (millis() - t0 > 200) break;
            delay(1);
        }
        if (s_nau.available()) {
            total += s_nau.getReading();
            count++;
        }
    }
    if (count == 0) return 0.0f;
    return (float)(total / count - s_offset);
}

float nau7802_get_cal_weight() { return s_cal_weight; }

void nau7802_adjust_cal_weight(float delta) {
    s_cal_weight += delta;
    if (s_cal_weight < 1.0f) s_cal_weight = 1.0f;
    LOGI(TAG, "Cal weight adjusted by %.1f -> %.1f g", delta, s_cal_weight);
}

void nau7802_set_cal_weight(float value) {
    s_cal_weight = (value < 1.0f) ? 1.0f : value;
    LOGI(TAG, "Cal weight set to %.1f g", s_cal_weight);
}

float nau7802_calibrate_with_cal_weight() {
    if (!s_available) return 0.0f;
    float raw_delta = nau7802_get_value(20);
    if (raw_delta == 0.0f) {
        LOGW(TAG, "Calibrate failed: raw delta is zero");
        return 0.0f;
    }
    float factor = raw_delta / s_cal_weight;
    LOGI(TAG, "Calibrate: raw_delta=%.1f / cal_weight=%.1f -> factor=%.4f", raw_delta, s_cal_weight, factor);
    nau7802_set_calibration(factor);
    return factor;
}

void nau7802_request_persist() { s_persist_requested = true; }

void nau7802_request_tare() {
    s_tare_requested = true;
    s_tare_persist   = true;
    s_status = SCALE_TARING;
}

void nau7802_request_tare_no_persist() {
    s_tare_requested = true;
    s_tare_persist   = false;
    s_status = SCALE_TARING;
}

void nau7802_request_calibrate() {
    s_calibrate_requested = true;
    s_status = SCALE_CALIBRATING;
}

const char* nau7802_get_status() {
    switch (s_status) {
        case SCALE_TARING:      return "taring";
        case SCALE_CALIBRATING: return "calibrating";
        default:                return "idle";
    }
}

// ============================================================================
// Sensor callback wiring
// ============================================================================

static void nau7802_init_cb() {
    if (s_initialized) return;
    s_initialized = true;

    // Initialize I2C on the sensor pins (Wire1 to avoid conflict with touch)
    Wire1.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL, SENSOR_I2C_FREQUENCY);

    if (!s_nau.begin(Wire1, true)) {
        LOGE(TAG, "NAU7802 not detected on I2C (SDA=%d SCL=%d)", SENSOR_I2C_SDA, SENSOR_I2C_SCL);
        return;
    }

    // Configure for load cell use: gain 128, 40 SPS (lower SPS = longer sinc filter window = less noise)
    s_nau.setGain(NAU7802_GAIN_128);
    s_nau.setSampleRate(NAU7802_SPS_40);

    // Internal calibration
    if (!s_nau.calibrateAFE()) {
        LOGW(TAG, "NAU7802 internal AFE calibration failed (continuing anyway)");
    }

    // Load calibration from NVS
    extern DeviceConfig device_config;
    float cal = strtof(device_config.scale_cal_factor, nullptr);
    long  ofs = strtol(device_config.scale_offset, nullptr, 10);
    if (cal == 0.0f) cal = 1.0f;

    s_cal_factor = cal;
    s_offset     = ofs;

    s_available = true;
    LOGI(TAG, "NAU7802 ready (SDA=%d SCL=%d cal=%.4f ofs=%ld)",
         SENSOR_I2C_SDA, SENSOR_I2C_SCL, cal, ofs);

    // Apply smoothing preset from config
    scale_smoothing_apply(device_config.scale_smoothing);
}

static void nau7802_loop_cb() {
    if (s_tare_requested && s_available) {
        s_tare_requested = false;
        bool persist = s_tare_persist;
        s_tare_persist = true;
        nau7802_tare();
        if (persist) s_persist_requested = true;
        s_status = SCALE_IDLE;
    }

    if (s_calibrate_requested && s_available) {
        s_calibrate_requested = false;
        float factor = nau7802_calibrate_with_cal_weight();
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
    brew_tick();
#endif

    if (s_persist_requested && s_available) {
        s_persist_requested = false;
        extern DeviceConfig device_config;
        snprintf(device_config.scale_cal_factor, CONFIG_SCALE_CAL_MAX_LEN, "%.4f", s_cal_factor);
        snprintf(device_config.scale_offset, CONFIG_SCALE_CAL_MAX_LEN, "%ld", s_offset);
        config_manager_save(&device_config);
        LOGI(TAG, "Calibration persisted to NVS (factor=%.4f offset=%ld)", s_cal_factor, s_offset);
    }
}

void nau7802_apply_preset(uint8_t idx) {
    scale_smoothing_apply(idx);
}

static void nau7802_append_api(JsonObject &doc) {
    if (!s_available) return;
    sensor_manager_set_number(doc, "weight_g", s_smooth_state.weight_display, true);
    sensor_manager_set_number(doc, "flow_rate", s_smooth_state.flow_rate, true);
}

static void nau7802_append_mqtt(JsonObject &doc) {
    nau7802_append_api(doc);
}

void register_nau7802_sensor(SensorRegistry &registry) {
    SensorCallbacks callbacks = {};
    callbacks.name        = "NAU7802";
    callbacks.init        = nau7802_init_cb;
    callbacks.loop        = nau7802_loop_cb;
    callbacks.append_api  = nau7802_append_api;
    callbacks.append_mqtt = nau7802_append_mqtt;
    registry.add(callbacks);
}

#endif // HAS_SENSOR_NAU7802

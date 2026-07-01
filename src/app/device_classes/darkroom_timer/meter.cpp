#include "meter.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "binding_template.h"
#include "expose_timer.h"
#include "log_manager.h"
#include "sensors/tsl2591_sensor.h"

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>

#define TAG "Meter"

// ============================================================================
// SBR-to-Grade lookup table
// ============================================================================

struct SbrGradeEntry {
    float sbr_max;
    float grade;
    const char* label;
};

static constexpr SbrGradeEntry SBR_GRADE_MAP[] = {
    { 0.60f, 5.0f, "Very flat" },
    { 0.70f, 4.5f, "Flat" },
    { 0.80f, 4.0f, "Flat" },
    { 0.90f, 3.5f, "Slightly flat" },
    { 1.00f, 3.0f, "Normal-" },
    { 1.10f, 2.5f, "Normal" },
    { 1.20f, 2.0f, "Normal" },
    { 1.35f, 1.5f, "Contrasty" },
    { 1.50f, 1.0f, "Contrasty" },
    { 1.65f, 0.5f, "Very contrasty" },
    { 999.f, 0.0f, "Extremely contrasty" },
};

static constexpr size_t SBR_GRADE_MAP_SIZE = sizeof(SBR_GRADE_MAP) / sizeof(SBR_GRADE_MAP[0]);

static float sbr_to_grade(float sbr) {
    for (size_t i = 0; i < SBR_GRADE_MAP_SIZE; i++) {
        if (sbr <= SBR_GRADE_MAP[i].sbr_max) return SBR_GRADE_MAP[i].grade;
    }
    return 0.0f;
}

static const char* sbr_to_label(float sbr) {
    for (size_t i = 0; i < SBR_GRADE_MAP_SIZE; i++) {
        if (sbr <= SBR_GRADE_MAP[i].sbr_max) return SBR_GRADE_MAP[i].label;
    }
    return "Extremely contrasty";
}

// ============================================================================
// State — stateless model, all values independent
// ============================================================================

static struct {
    // User inputs
    float lref;            // bare-bulb reference (lux), 0 = not set
    float zone5_time;      // Zone V time (seconds), 0 = not set

    // Sensor readings
    float l_bright;        // bright spot (lux), -1 = not read
    float l_dark;          // dark spot (lux), -1 = not read

    // Computed results (recalculated on any input change)
    float sbr;             // Subject Brightness Range
    float grade;           // recommended grade
    const char* grade_label;
    float time_s;          // recommended exposure time, -1 = not computable
    bool has_results;

    // Magnification compensation
    float mag_lux_a;       // -1.0f = not set
    float mag_lux_b;       // -1.0f = not set
    uint8_t mag_generation;  // incremented by mag_clear to invalidate in-flight reads
    uint8_t mag_a_gen;       // generation at time of mag_measure_a request
    uint8_t mag_b_gen;       // generation at time of mag_measure_b request
} g_meter = {
    .lref        = 0.0f,
    .zone5_time  = 0.0f,
    .l_bright    = -1.0f,
    .l_dark      = -1.0f,
    .sbr         = 0.0f,
    .grade       = 0.0f,
    .grade_label = "---",
    .time_s      = -1.0f,
    .has_results = false,
    .mag_lux_a   = -1.0f,
    .mag_lux_b   = -1.0f,
    .mag_generation = 0,
    .mag_a_gen   = 0,
    .mag_b_gen   = 0,
};

static portMUX_TYPE g_meter_lock = portMUX_INITIALIZER_UNLOCKED;

// Callback-based deferred sensor read (reject-if-busy)
static MeterReadCallback s_pending_callback = nullptr;

// ============================================================================
// Computation — called after any input changes
// ============================================================================
// Called under g_meter_lock from sensor-read callbacks (Arduino loop task)
// and without lock from LVGL-task commands (cmd_set_*, cmd_adjust_*) where
// the higher-priority LVGL task cannot be preempted by the loop task.
//
// Must not contain logging or I/O — called under spinlock from callbacks.

static void recompute() {
    // Need both bright and dark to compute anything
    if (g_meter.l_bright < 0.0f || g_meter.l_dark < 0.0f) {
        g_meter.has_results = false;
        g_meter.time_s = -1.0f;
        return;
    }

    float bright = g_meter.l_bright;
    float dark   = g_meter.l_dark;

    // Auto-swap if user read in wrong order
    if (bright < dark) {
        float tmp = bright;
        bright = dark;
        dark = tmp;
        g_meter.l_bright = bright;
        g_meter.l_dark   = dark;
    }

    // SBR
    if (dark > 0.0f) {
        g_meter.sbr = log10f(bright / dark);
    } else {
        g_meter.sbr = 99.0f;  // infinite contrast
    }
    g_meter.grade = sbr_to_grade(g_meter.sbr);
    g_meter.grade_label = sbr_to_label(g_meter.sbr);

    // Exposure time — only if calibration data available
    if (g_meter.lref > 0.0f && g_meter.zone5_time > 0.0f && bright > 0.0f && dark > 0.0f) {
        float d_bright = log10f(g_meter.lref / bright);
        float d_dark   = log10f(g_meter.lref / dark);
        float d_mid    = (d_bright + d_dark) / 2.0f;
        g_meter.time_s = g_meter.zone5_time * powf(10.0f, d_mid);
        g_meter.time_s = roundf(g_meter.time_s * 10.0f) / 10.0f;
    } else {
        g_meter.time_s = -1.0f;
    }

    g_meter.has_results = true;
}

// ============================================================================
// Sensor read callbacks (invoked from meter_loop, outside spinlock)
// ============================================================================

static void log_meter_results() {
    if (g_meter.has_results) {
        LOGI(TAG, "SBR=%.2f grade=%.1f (%s) time=%.1fs",
             g_meter.sbr, g_meter.grade, g_meter.grade_label, g_meter.time_s);
    }
}

static void on_read_lref(float lux) {
    portENTER_CRITICAL(&g_meter_lock);
    g_meter.lref = lux;
    recompute();
    portEXIT_CRITICAL(&g_meter_lock);
    LOGI(TAG, "Lref = %.4f lux", lux);
    log_meter_results();
}

static void on_read_bright(float lux) {
    portENTER_CRITICAL(&g_meter_lock);
    g_meter.l_bright = lux;
    recompute();
    portEXIT_CRITICAL(&g_meter_lock);
    LOGI(TAG, "Bright = %.4f lux", lux);
    log_meter_results();
}

static void on_read_dark(float lux) {
    portENTER_CRITICAL(&g_meter_lock);
    g_meter.l_dark = lux;
    recompute();
    portEXIT_CRITICAL(&g_meter_lock);
    LOGI(TAG, "Dark = %.4f lux", lux);
    log_meter_results();
}

static void on_mag_a(float lux) {
    portENTER_CRITICAL(&g_meter_lock);
    if (g_meter.mag_a_gen == g_meter.mag_generation) {
        g_meter.mag_lux_a = lux;
    }
    portEXIT_CRITICAL(&g_meter_lock);
    LOGI(TAG, "Mag A = %.4f lux", lux);
}

static void on_mag_b(float lux) {
    portENTER_CRITICAL(&g_meter_lock);
    if (g_meter.mag_b_gen == g_meter.mag_generation) {
        g_meter.mag_lux_b = lux;
    }
    portEXIT_CRITICAL(&g_meter_lock);
    LOGI(TAG, "Mag B = %.4f lux", lux);
}

// ============================================================================
// Commands (called from LVGL task via action_dispatch)
// ============================================================================

static void cmd_read_lref() {
    if (!meter_request_read(on_read_lref)) {
        LOGW(TAG, "read_lref rejected — read busy");
    }
}

static void cmd_read_bright() {
    if (!meter_request_read(on_read_bright)) {
        LOGW(TAG, "read_bright rejected — read busy");
    }
}

static void cmd_read_dark() {
    if (!meter_request_read(on_read_dark)) {
        LOGW(TAG, "read_dark rejected — read busy");
    }
}

static void cmd_set_lref(float v) {
    if (v < 0.0f) v = 0.0f;
    g_meter.lref = v;
    LOGI(TAG, "Set Lref = %.4f", v);
    recompute();
}

static void cmd_adjust_lref(float delta) {
    g_meter.lref += delta;
    if (g_meter.lref < 0.0f) g_meter.lref = 0.0f;
    LOGI(TAG, "Adjust Lref %.1f → %.1f", delta, g_meter.lref);
    recompute();
}

static void cmd_set_zone5(float v) {
    if (v < 0.0f) v = 0.0f;
    g_meter.zone5_time = roundf(v * 10.0f) / 10.0f;
    LOGI(TAG, "Set Zone V = %.1fs", g_meter.zone5_time);
    recompute();
}

static void cmd_adjust_zone5(float delta) {
    g_meter.zone5_time += delta;
    if (g_meter.zone5_time < 0.0f) g_meter.zone5_time = 0.0f;
    g_meter.zone5_time = roundf(g_meter.zone5_time * 10.0f) / 10.0f;
    LOGI(TAG, "Adjust Zone V %.1f → %.1fs", delta, g_meter.zone5_time);
    recompute();
}

static void cmd_mag_measure_a() {
    portENTER_CRITICAL(&g_meter_lock);
    g_meter.mag_a_gen = g_meter.mag_generation;
    portEXIT_CRITICAL(&g_meter_lock);
    if (!meter_request_read(on_mag_a)) {
        LOGW(TAG, "mag_measure_a rejected — read busy");
    }
}

static void cmd_mag_measure_b() {
    portENTER_CRITICAL(&g_meter_lock);
    g_meter.mag_b_gen = g_meter.mag_generation;
    portEXIT_CRITICAL(&g_meter_lock);
    if (!meter_request_read(on_mag_b)) {
        LOGW(TAG, "mag_measure_b rejected — read busy");
    }
}

static void cmd_mag_clear() {
    portENTER_CRITICAL(&g_meter_lock);
    g_meter.mag_lux_a = -1.0f;
    g_meter.mag_lux_b = -1.0f;
    g_meter.mag_generation++;
    portEXIT_CRITICAL(&g_meter_lock);
    LOGI(TAG, "Mag compensation cleared");
}

// ============================================================================
// Binding resolver
// ============================================================================

static bool meter_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        snprintf(out, out_len, "ERR:no_key");
        return false;
    }

    if (strcmp(params, "lref") == 0) {
        if (g_meter.lref <= 0.0f) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.4f", g_meter.lref);
        return true;
    }

    if (strcmp(params, "zone5_time") == 0) {
        if (g_meter.zone5_time <= 0.0f) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.1f", g_meter.zone5_time);
        return true;
    }

    if (strcmp(params, "l_bright") == 0) {
        if (g_meter.l_bright < 0.0f) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.4f", g_meter.l_bright);
        return true;
    }

    if (strcmp(params, "l_dark") == 0) {
        if (g_meter.l_dark < 0.0f) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.4f", g_meter.l_dark);
        return true;
    }

    if (strcmp(params, "sbr") == 0) {
        if (!g_meter.has_results) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.2f", g_meter.sbr);
        return true;
    }

    if (strcmp(params, "grade") == 0) {
        if (!g_meter.has_results) { snprintf(out, out_len, "---"); return false; }
        if (g_meter.grade == (int)g_meter.grade) {
            snprintf(out, out_len, "%d", (int)g_meter.grade);
        } else {
            snprintf(out, out_len, "%.1f", g_meter.grade);
        }
        return true;
    }

    if (strcmp(params, "grade_label") == 0) {
        if (!g_meter.has_results) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%s", g_meter.grade_label);
        return true;
    }

    if (strcmp(params, "time") == 0) {
        if (!g_meter.has_results || g_meter.time_s < 0.0f) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.1f", g_meter.time_s);
        return true;
    }

    // Magnification compensation bindings
    static constexpr float MIN_VALID_LUX = 0.001f;

    if (strcmp(params, "mag_lux_a") == 0) {
        portENTER_CRITICAL(&g_meter_lock);
        float v = g_meter.mag_lux_a;
        portEXIT_CRITICAL(&g_meter_lock);
        if (v < 0.0f) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.4f", v);
        return true;
    }

    if (strcmp(params, "mag_lux_b") == 0) {
        portENTER_CRITICAL(&g_meter_lock);
        float v = g_meter.mag_lux_b;
        portEXIT_CRITICAL(&g_meter_lock);
        if (v < 0.0f) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.4f", v);
        return true;
    }

    if (strcmp(params, "mag_factor") == 0) {
        portENTER_CRITICAL(&g_meter_lock);
        float a = g_meter.mag_lux_a, b = g_meter.mag_lux_b;
        portEXIT_CRITICAL(&g_meter_lock);
        if (a < 0.0f || b < MIN_VALID_LUX) {
            snprintf(out, out_len, "---"); return false;
        }
        float factor = a / b;
        if (!isfinite(factor)) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.1f", factor);
        return true;
    }

    if (strcmp(params, "mag_time") == 0) {
        portENTER_CRITICAL(&g_meter_lock);
        float a = g_meter.mag_lux_a, b = g_meter.mag_lux_b;
        portEXIT_CRITICAL(&g_meter_lock);
        if (a < 0.0f || b < MIN_VALID_LUX) {
            snprintf(out, out_len, "---"); return false;
        }
        float set_time = expose_timer_get_time();
        if (set_time <= 0.0f) { snprintf(out, out_len, "---"); return false; }
        float rec = set_time * (a / b);
        if (!isfinite(rec)) { snprintf(out, out_len, "---"); return false; }
        rec = roundf(rec * 10.0f) / 10.0f;
        snprintf(out, out_len, "%.1f", rec);
        return true;
    }

    snprintf(out, out_len, "ERR:bad_key");
    return false;
}

static void meter_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Public API
// ============================================================================

void meter_dispatch(const char* command, const char* value) {
    if (!command || !command[0]) {
        LOGW(TAG, "Empty command");
        return;
    }

    if (strcmp(command, "read_lref") == 0)   { cmd_read_lref(); return; }
    if (strcmp(command, "read_bright") == 0) { cmd_read_bright(); return; }
    if (strcmp(command, "read_dark") == 0)   { cmd_read_dark(); return; }

    if (strcmp(command, "mag_measure_a") == 0) { cmd_mag_measure_a(); return; }
    if (strcmp(command, "mag_measure_b") == 0) { cmd_mag_measure_b(); return; }
    if (strcmp(command, "mag_clear") == 0)     { cmd_mag_clear(); return; }

    if (strcmp(command, "set_lref") == 0) {
        cmd_set_lref(strtof(value, NULL));
        return;
    }
    if (strcmp(command, "adjust_lref") == 0) {
        cmd_adjust_lref(strtof(value, NULL));
        return;
    }
    if (strcmp(command, "set_zone5") == 0) {
        cmd_set_zone5(strtof(value, NULL));
        return;
    }
    if (strcmp(command, "adjust_zone5") == 0) {
        cmd_adjust_zone5(strtof(value, NULL));
        return;
    }

    LOGW(TAG, "Unknown command: '%s'", command);
}

bool meter_request_read(MeterReadCallback on_complete) {
    portENTER_CRITICAL(&g_meter_lock);
    if (s_pending_callback != nullptr) {
        portEXIT_CRITICAL(&g_meter_lock);
        LOGW(TAG, "Read busy — rejected");
        return false;
    }
    s_pending_callback = on_complete;
    portEXIT_CRITICAL(&g_meter_lock);
    return true;
}

void meter_loop() {
    portENTER_CRITICAL(&g_meter_lock);
    MeterReadCallback cb = s_pending_callback;
    s_pending_callback = nullptr;
    portEXIT_CRITICAL(&g_meter_lock);
    if (!cb) return;

    float lux = tsl2591_read_lux();
    if (lux < 0.0f) {
        LOGW(TAG, "Sensor read failed");
        return;
    }

    // Callback runs OUTSIDE spinlock — it acquires lock internally if needed
    cb(lux);
}

float meter_get_lref() {
    return g_meter.lref;  // atomic float read on ESP32
}

float meter_get_zone5_time() {
    return g_meter.zone5_time;
}

float meter_get_bright() {
    return g_meter.l_bright;
}

float meter_get_dark() {
    return g_meter.l_dark;
}

float meter_get_sbr() {
    return g_meter.sbr;
}

float meter_get_grade() {
    return g_meter.grade;
}

const char* meter_get_grade_label() {
    return g_meter.grade_label;
}

float meter_get_mag_factor() {
    portENTER_CRITICAL(&g_meter_lock);
    float a = g_meter.mag_lux_a, b = g_meter.mag_lux_b;
    portEXIT_CRITICAL(&g_meter_lock);
    if (a < 0.0f || b < 0.001f) return -1.0f;
    float factor = a / b;
    if (!isfinite(factor)) return -1.0f;
    return factor;
}

#if HAS_MCP
float meter_get_time() {
    return g_meter.time_s;  // atomic float read on ESP32
}

float meter_get_mag_lux_a() {
    portENTER_CRITICAL(&g_meter_lock);
    float a = g_meter.mag_lux_a;
    portEXIT_CRITICAL(&g_meter_lock);
    return a;
}

float meter_get_mag_lux_b() {
    portENTER_CRITICAL(&g_meter_lock);
    float b = g_meter.mag_lux_b;
    portEXIT_CRITICAL(&g_meter_lock);
    return b;
}

bool meter_get_has_results() {
    return g_meter.has_results;
}
#endif // HAS_MCP

#if HAS_MCP
#include <ArduinoJson.h>
static void meter_scheme_describe(void* out) {
    JsonObject& o = *static_cast<JsonObject*>(out);
    o["syntax"]  = "[meter:key] or [meter:key;format]";
    o["example"] = "Grade [meter:grade_label] @ [meter:time;%.1f]s";
    o["keys"]    = "lref, l_bright, l_dark, sbr, grade, grade_label, time, mag_lux_a, mag_lux_b, mag_time, mag_factor";
    o["note"]    = "Darkroom enlarging-meter readings and recommended grade/time.";
}
#endif

void meter_init() {
    if (!binding_template_register("meter", meter_resolve, meter_collect)) {
        LOGE(TAG, "Failed to register meter binding scheme");
    } else {
        LOGI(TAG, "Meter binding scheme registered");
    }
#if HAS_MCP
    binding_template_set_scheme_describe("meter", meter_scheme_describe);
#endif
}

#else // !IS_DARKROOM_TIMER

void meter_init() {}
void meter_dispatch(const char*, const char*) {}
void meter_loop() {}
bool meter_request_read(MeterReadCallback) { return false; }
float meter_get_lref() { return 0.0f; }
float meter_get_zone5_time() { return 0.0f; }
float meter_get_bright() { return -1.0f; }
float meter_get_dark() { return -1.0f; }
float meter_get_sbr() { return 0.0f; }
float meter_get_grade() { return 0.0f; }
const char* meter_get_grade_label() { return "---"; }
float meter_get_mag_factor() { return -1.0f; }
#if HAS_MCP
float meter_get_time() { return -1.0f; }
float meter_get_mag_lux_a() { return -1.0f; }
float meter_get_mag_lux_b() { return -1.0f; }
bool meter_get_has_results() { return false; }
#endif // HAS_MCP

#endif // IS_DARKROOM_TIMER

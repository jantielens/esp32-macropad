#include "meter.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "binding_template.h"
#include "log_manager.h"
#include "relay_controller.h"
#include "sensors/tsl2591_sensor.h"
#include "shared_mem.h"

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
// State
// ============================================================================

enum MeterState : uint8_t {
    METER_IDLE            = 0,
    METER_AWAITING_BRIGHT = 1,
    METER_AWAITING_DARK   = 2,
    METER_RESULTS         = 3,
};

enum PendingRead : uint8_t {
    PENDING_NONE   = 0,
    PENDING_BRIGHT = 1,
    PENDING_DARK   = 2,
};

static struct {
    MeterState state;

    // User inputs
    float lref;            // bare-bulb reference (lux), 0 = not set
    float zone5_time;      // Zone V time (seconds), 0 = not set

    // Sensor readings
    float l_bright;        // bright spot (lux), -1 = not read
    float l_dark;          // dark spot (lux), -1 = not read

    // Computed results
    float sbr;             // Subject Brightness Range
    float grade;           // recommended grade
    const char* grade_label;
    float time_s;          // recommended exposure time, -1 = not computable

    bool has_results;

    // Deferred sensor read flag (set from LVGL task, consumed in loop)
    PendingRead pending_read;
} s = {
    .state       = METER_IDLE,
    .lref        = 0.0f,
    .zone5_time  = 0.0f,
    .l_bright    = -1.0f,
    .l_dark      = -1.0f,
    .sbr         = 0.0f,
    .grade       = 0.0f,
    .grade_label = "---",
    .time_s      = -1.0f,
    .has_results = false,
    .pending_read = PENDING_NONE,
};

static portMUX_TYPE g_meter_lock = portMUX_INITIALIZER_UNLOCKED;

// ============================================================================
// Computation
// ============================================================================

static void compute_results() {
    float bright = s.l_bright;
    float dark   = s.l_dark;

    // Auto-swap if user read in wrong order
    if (bright < dark) {
        float tmp = bright;
        bright = dark;
        dark = tmp;
        s.l_bright = bright;
        s.l_dark   = dark;
        LOGI(TAG, "Auto-swapped bright/dark");
    }

    // SBR — always computable
    if (dark > 0.0f) {
        s.sbr = log10f(bright / dark);
    } else {
        s.sbr = 99.0f;  // infinite contrast
    }
    s.grade = sbr_to_grade(s.sbr);
    s.grade_label = sbr_to_label(s.sbr);

    // Exposure time — only if calibration data available
    if (s.lref > 0.0f && s.zone5_time > 0.0f && bright > 0.0f && dark > 0.0f) {
        float d_bright = log10f(s.lref / bright);
        float d_dark   = log10f(s.lref / dark);
        float d_mid    = (d_bright + d_dark) / 2.0f;
        s.time_s = s.zone5_time * powf(10.0f, d_mid);
        // Snap to one decimal
        s.time_s = roundf(s.time_s * 10.0f) / 10.0f;
    } else {
        s.time_s = -1.0f;
    }

    s.has_results = true;

    LOGI(TAG, "SBR=%.2f grade=%.1f (%s) time=%.1fs",
         s.sbr, s.grade, s.grade_label, s.time_s);
}

// ============================================================================
// Lref refresh from shared memory
// ============================================================================

static void refresh_lref() {
    if (s.lref > 0.0f) return;  // already set (manual or previous refresh)
    bool is_set = false;
    float val = shared_mem_get("lref", &is_set);
    if (is_set && val > 0.0f) {
        s.lref = val;
        LOGI(TAG, "Lref auto-populated from shared memory: %.1f", val);
    }
}

// ============================================================================
// Commands (called from LVGL task via action_dispatch)
// ============================================================================

static void cmd_focus_on() {
    if (s.state == METER_AWAITING_BRIGHT || s.state == METER_AWAITING_DARK) {
        LOGD(TAG, "Focus ON ignored — already awaiting reading");
        return;
    }

    // Refresh Lref from shared memory at start of each metering cycle
    refresh_lref();

    // Clear previous readings but keep lref + zone5
    s.l_bright = -1.0f;
    s.l_dark   = -1.0f;
    s.has_results = false;
    s.time_s = -1.0f;

    s.state = METER_AWAITING_BRIGHT;
    relay_request(true);
    LOGI(TAG, "Focus ON — awaiting bright");
}

static void cmd_read_bright() {
    if (s.state != METER_AWAITING_BRIGHT) return;
    portENTER_CRITICAL(&g_meter_lock);
    s.pending_read = PENDING_BRIGHT;
    portEXIT_CRITICAL(&g_meter_lock);
}

static void cmd_read_dark() {
    if (s.state != METER_AWAITING_DARK) return;
    portENTER_CRITICAL(&g_meter_lock);
    s.pending_read = PENDING_DARK;
    portEXIT_CRITICAL(&g_meter_lock);
}

static void cmd_cancel() {
    if (s.state == METER_IDLE) return;

    s.state = METER_IDLE;
    s.l_bright = -1.0f;
    s.l_dark   = -1.0f;
    s.has_results = false;
    s.time_s = -1.0f;
    portENTER_CRITICAL(&g_meter_lock);
    s.pending_read = PENDING_NONE;
    portEXIT_CRITICAL(&g_meter_lock);
    relay_request(false);
    LOGI(TAG, "Cancelled");
}

static void cmd_set_lref(float v) {
    if (v < 0.0f) v = 0.0f;
    s.lref = v;
    LOGI(TAG, "Set Lref = %.1f", v);
}

static void cmd_add_lref(float delta) {
    s.lref += delta;
    if (s.lref < 0.0f) s.lref = 0.0f;
    LOGI(TAG, "Add Lref %.1f → %.1f", delta, s.lref);
}

static void cmd_set_zone5(float v) {
    if (v < 0.0f) v = 0.0f;
    s.zone5_time = roundf(v * 10.0f) / 10.0f;
    LOGI(TAG, "Set Zone V = %.1fs", s.zone5_time);
}

static void cmd_add_zone5(float delta) {
    s.zone5_time += delta;
    if (s.zone5_time < 0.0f) s.zone5_time = 0.0f;
    s.zone5_time = roundf(s.zone5_time * 10.0f) / 10.0f;
    LOGI(TAG, "Add Zone V %.1f → %.1fs", delta, s.zone5_time);
}

// ============================================================================
// Binding resolver
// ============================================================================

static bool meter_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        snprintf(out, out_len, "ERR:no_key");
        return false;
    }

    if (strcmp(params, "state") == 0) {
        const char* st;
        switch (s.state) {
            case METER_AWAITING_BRIGHT: st = "awaiting_bright"; break;
            case METER_AWAITING_DARK:   st = "awaiting_dark";   break;
            case METER_RESULTS:         st = "results";         break;
            default:                    st = "idle";            break;
        }
        snprintf(out, out_len, "%s", st);
        return true;
    }

    if (strcmp(params, "lref") == 0) {
        if (s.lref <= 0.0f) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.0f", s.lref);
        return true;
    }

    if (strcmp(params, "zone5_time") == 0) {
        if (s.zone5_time <= 0.0f) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.1f", s.zone5_time);
        return true;
    }

    if (strcmp(params, "l_bright") == 0) {
        if (s.l_bright < 0.0f) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.1f", s.l_bright);
        return true;
    }

    if (strcmp(params, "l_dark") == 0) {
        if (s.l_dark < 0.0f) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.1f", s.l_dark);
        return true;
    }

    if (strcmp(params, "sbr") == 0) {
        if (!s.has_results) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.2f", s.sbr);
        return true;
    }

    if (strcmp(params, "grade") == 0) {
        if (!s.has_results) { snprintf(out, out_len, "---"); return false; }
        // Show as integer if whole number, else one decimal
        if (s.grade == (int)s.grade) {
            snprintf(out, out_len, "%d", (int)s.grade);
        } else {
            snprintf(out, out_len, "%.1f", s.grade);
        }
        return true;
    }

    if (strcmp(params, "grade_label") == 0) {
        if (!s.has_results) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%s", s.grade_label);
        return true;
    }

    if (strcmp(params, "time") == 0) {
        if (!s.has_results || s.time_s < 0.0f) { snprintf(out, out_len, "---"); return false; }
        snprintf(out, out_len, "%.1f", s.time_s);
        return true;
    }

    if (strcmp(params, "relay") == 0) {
        snprintf(out, out_len, "%s", relay_is_on() ? "ON" : "OFF");
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

void meter_dispatch(const char* command) {
    if (!command || !command[0]) {
        LOGW(TAG, "Empty command");
        return;
    }

    if (strcmp(command, "focus_on") == 0)    { cmd_focus_on(); return; }
    if (strcmp(command, "read_bright") == 0) { cmd_read_bright(); return; }
    if (strcmp(command, "read_dark") == 0)   { cmd_read_dark(); return; }
    if (strcmp(command, "cancel") == 0)      { cmd_cancel(); return; }

    if (strncmp(command, "set_lref:", 9) == 0) {
        cmd_set_lref(strtof(command + 9, NULL));
        return;
    }
    if (strncmp(command, "add_lref:", 9) == 0) {
        cmd_add_lref(strtof(command + 9, NULL));
        return;
    }
    if (strncmp(command, "set_zone5:", 10) == 0) {
        cmd_set_zone5(strtof(command + 10, NULL));
        return;
    }
    if (strncmp(command, "add_zone5:", 10) == 0) {
        cmd_add_zone5(strtof(command + 10, NULL));
        return;
    }

    LOGW(TAG, "Unknown command: '%s'", command);
}

void meter_tick() {
    // No time-based expiry — all sequencing is in meter_loop().
}

void meter_loop() {
    portENTER_CRITICAL(&g_meter_lock);
    PendingRead pr = s.pending_read;
    s.pending_read = PENDING_NONE;
    portEXIT_CRITICAL(&g_meter_lock);
    if (pr == PENDING_NONE) return;

    float lux = tsl2591_read_lux();

    if (pr == PENDING_BRIGHT) {
        if (lux < 0.0f) {
            LOGW(TAG, "Bright spot read failed");
            return;  // stay in awaiting_bright, user can retry
        }
        s.l_bright = lux;
        s.state = METER_AWAITING_DARK;
        LOGI(TAG, "Bright = %.1f lux — awaiting dark", lux);
    } else if (pr == PENDING_DARK) {
        if (lux < 0.0f) {
            LOGW(TAG, "Dark spot read failed");
            return;  // stay in awaiting_dark, user can retry
        }
        s.l_dark = lux;

        // Turn enlarger off after dark reading
        relay_request(false);

        compute_results();
        s.state = METER_RESULTS;
    }
}

void meter_init() {
    // Auto-populate Lref from shared memory (Phase 1a)
    refresh_lref();

    if (!binding_template_register("meter", meter_resolve, meter_collect)) {
        LOGE(TAG, "Failed to register meter binding scheme");
    } else {
        LOGI(TAG, "Meter binding scheme registered");
    }
}

#else // !IS_DARKROOM_TIMER

void meter_init() {}
void meter_dispatch(const char*) {}
void meter_tick() {}
void meter_loop() {}

#endif // IS_DARKROOM_TIMER

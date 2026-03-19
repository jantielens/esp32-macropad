#include "brew_manager.h"

#if HAS_SENSOR_HX711

#include "brew_log.h"
#include "sensors/hx711_sensor.h"
#include "log_manager.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdio.h>

#define TAG "Brew"

// ============================================================================
// State
// ============================================================================

static BrewPhase s_phase       = BREW_IDLE;
static uint32_t  s_start_ms    = 0;    // millis() when brewing started
static uint32_t  s_elapsed_ms  = 0;    // frozen elapsed when stopped

// Series recording (PSRAM)
static BrewSample* s_series    = nullptr;
static uint16_t    s_series_count = 0;
static uint32_t    s_last_sample_ms = 0;  // millis() of last 1 Hz sample

// Deferred save — brew_stop() runs on the LVGL task whose stack lives in
// PSRAM.  Flash I/O (LittleFS + NVS) disables the cache, making a PSRAM
// stack inaccessible and triggering an assert.  We set a flag here and
// perform the actual save in brew_tick(), which runs on the main Arduino
// loop task (internal-RAM stack, flash-safe).
static bool        s_save_pending = false;
static float       s_save_weight  = 0;

// ============================================================================
// Control API
// ============================================================================

void brew_start() {
    LOGI(TAG, "Start: tare + arm");
    hx711_request_tare();
    s_elapsed_ms = 0;
    s_start_ms   = 0;
    s_series_count = 0;
    s_last_sample_ms = 0;

    // Allocate series buffer in PSRAM
    if (!s_series) {
        s_series = (BrewSample*)heap_caps_malloc(
            BREW_SERIES_MAX_SAMPLES * sizeof(BrewSample),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_series) {
            LOGW(TAG, "PSRAM alloc failed for series buffer");
        }
    }

    s_phase = BREW_READY;
}

void brew_stop() {
    if (s_phase == BREW_BREWING) {
        s_elapsed_ms = millis() - s_start_ms;
        s_phase = BREW_DONE;
        LOGI(TAG, "Stop: elapsed=%lu ms, weight=%.1f g",
             (unsigned long)s_elapsed_ms, hx711_get_weight());

        // Defer save to brew_tick() (runs on main loop task with internal-RAM stack)
        s_save_weight  = hx711_get_weight();
        s_save_pending = true;
    } else if (s_phase == BREW_READY) {
        s_phase = BREW_DONE;
        brew_free_series();
        LOGI(TAG, "Stop: cancelled from READY");
    }
}

void brew_reset() {
    s_phase      = BREW_IDLE;
    s_elapsed_ms = 0;
    s_start_ms   = 0;
    brew_free_series();
    LOGI(TAG, "Reset");
}

// ============================================================================
// Tick — called every sensor poll cycle
// ============================================================================

static void record_sample() {
    if (!s_series || s_series_count >= BREW_SERIES_MAX_SAMPLES) return;
    s_series[s_series_count].weight = hx711_get_weight();
    s_series[s_series_count].flow   = hx711_get_flow_rate();
    s_series_count++;
}

void brew_tick() {
    // Deferred save — runs on main task (internal RAM stack, flash-safe)
    if (s_save_pending) {
        s_save_pending = false;
        brew_log_save(s_elapsed_ms, s_save_weight, s_series, s_series_count);
        brew_free_series();
        LOGI(TAG, "Brew saved to flash");
    }

    if (s_phase == BREW_READY) {
        float w = hx711_get_weight();
        if (w >= BREW_AUTO_START_THRESHOLD_G) {
            s_start_ms = millis();
            s_last_sample_ms = s_start_ms;
            s_series_count = 0;
            record_sample();  // first sample at t=0
            s_phase = BREW_BREWING;
            LOGI(TAG, "Auto-start: weight=%.1f g", w);
        }
        return;
    }

    if (s_phase == BREW_BREWING) {
        uint32_t now = millis();
        if (now - s_last_sample_ms >= 1000) {
            s_last_sample_ms += 1000;
            record_sample();
        }
    }
}

// ============================================================================
// Query API
// ============================================================================

BrewPhase brew_get_phase() {
    return s_phase;
}

const char* brew_get_phase_name() {
    switch (s_phase) {
        case BREW_IDLE:    return "Idle";
        case BREW_READY:   return "Ready";
        case BREW_BREWING: return "Brewing";
        case BREW_DONE:    return "Done";
        default:           return "?";
    }
}

uint32_t brew_get_timer_ms() {
    switch (s_phase) {
        case BREW_BREWING: return millis() - s_start_ms;
        case BREW_DONE:    return s_elapsed_ms;
        default:           return 0;
    }
}

float brew_get_weight() {
    return hx711_get_weight();
}

float brew_get_flow_rate() {
    return hx711_get_flow_rate();
}

bool brew_is_active() {
    return s_phase == BREW_READY || s_phase == BREW_BREWING;
}

// ============================================================================
// Series access
// ============================================================================

void brew_free_series() {
    if (s_series) {
        heap_caps_free(s_series);
        s_series = nullptr;
    }
    s_series_count = 0;
}

// ============================================================================
// Timer formatting (same logic as timer_engine, self-contained)
// ============================================================================

int brew_format_timer(const char* fmt, char* out, size_t out_len) {
    uint32_t ms = brew_get_timer_ms();
    uint32_t total_s = ms / 1000;
    uint32_t h  = total_s / 3600;
    uint32_t m  = (total_s % 3600) / 60;
    uint32_t s  = total_s % 60;
    uint32_t ds = (ms % 1000) / 100;

    if (!fmt || !fmt[0] || strcmp(fmt, "mm:ss") == 0) {
        return snprintf(out, out_len, "%u:%02u", (unsigned)(h * 60 + m), (unsigned)s);
    } else if (strcmp(fmt, "hh:mm:ss") == 0) {
        return snprintf(out, out_len, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    } else if (strcmp(fmt, "ss") == 0) {
        return snprintf(out, out_len, "%u", (unsigned)total_s);
    } else if (strcmp(fmt, "mm:ss.d") == 0) {
        return snprintf(out, out_len, "%u:%02u.%u", (unsigned)(h * 60 + m), (unsigned)s, (unsigned)ds);
    }
    return snprintf(out, out_len, "%u:%02u", (unsigned)(h * 60 + m), (unsigned)s);
}

// ============================================================================
// Init
// ============================================================================

void brew_manager_init() {
    s_phase      = BREW_IDLE;
    s_elapsed_ms = 0;
    s_start_ms   = 0;
    s_series     = nullptr;
    s_series_count = 0;
    LOGI(TAG, "Init");
}

#endif // HAS_SENSOR_HX711

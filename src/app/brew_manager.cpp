#include "brew_manager.h"

#if HAS_DISPLAY && HAS_SENSOR_HX711

#include "sensors/hx711_sensor.h"
#include "log_manager.h"

#include <Arduino.h>
#include <string.h>
#include <stdio.h>

#define TAG "Brew"

// ============================================================================
// State
// ============================================================================

static BrewPhase s_phase       = BREW_IDLE;
static uint32_t  s_start_ms    = 0;    // millis() when brewing started
static uint32_t  s_elapsed_ms  = 0;    // frozen elapsed when stopped

// ============================================================================
// Control API
// ============================================================================

void brew_start() {
    LOGI(TAG, "Start: tare + arm");
    hx711_request_tare();
    s_elapsed_ms = 0;
    s_start_ms   = 0;
    s_phase      = BREW_READY;
}

void brew_stop() {
    if (s_phase == BREW_BREWING) {
        s_elapsed_ms = millis() - s_start_ms;
        s_phase = BREW_DONE;
        LOGI(TAG, "Stop: elapsed=%lu ms, weight=%.1f g",
             (unsigned long)s_elapsed_ms, hx711_get_weight());
    } else if (s_phase == BREW_READY) {
        s_phase = BREW_DONE;
        LOGI(TAG, "Stop: cancelled from READY");
    }
}

void brew_reset() {
    s_phase      = BREW_IDLE;
    s_elapsed_ms = 0;
    s_start_ms   = 0;
    LOGI(TAG, "Reset");
}

// ============================================================================
// Tick — called every sensor poll cycle
// ============================================================================

void brew_tick() {
    if (s_phase != BREW_READY) return;

    float w = hx711_get_weight();
    if (w >= BREW_AUTO_START_THRESHOLD_G) {
        s_start_ms = millis();
        s_phase    = BREW_BREWING;
        LOGI(TAG, "Auto-start: weight=%.1f g", w);
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
    LOGI(TAG, "Init");
}

#endif // HAS_DISPLAY && HAS_SENSOR_HX711

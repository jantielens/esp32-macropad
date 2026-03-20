#include "brew_manager.h"

#if HAS_SENSOR_HX711

#include "brew_log.h"
#include "brew_templates.h"
#include "sensors/hx711_sensor.h"
#include "log_manager.h"

#if HAS_AUDIO
#include "audio.h"
#endif

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdio.h>

#define TAG "Brew"

// ============================================================================
// State
// ============================================================================

static BrewPhase            s_phase         = BREW_IDLE;
static const BrewTemplate*  s_template      = nullptr;
// Survives brew_reset() so [brew:next_label] shows the correct start_label
// while Idle (before the first or next brew starts).
static const BrewTemplate*  s_last_template = nullptr;
static uint8_t              s_stage_index   = 0;   // index into s_template->stages
static bool                 s_timer_running = false;
static uint32_t             s_start_ms      = 0;   // millis() when timer started
static uint32_t             s_elapsed_ms    = 0;   // frozen elapsed when stopped
static float                s_dose_weight   = 0.0f;

// Per-stage entry time for STAGE_AUTO_TIME
static uint32_t             s_stage_enter_ms = 0;

// Series recording (PSRAM)
static BrewSample* s_series         = nullptr;
static uint16_t    s_series_count   = 0;
static uint32_t    s_last_sample_ms = 0;

// Markers
static BrewMarker  s_markers[BREW_MARKER_MAX];
static uint8_t     s_marker_count  = 0;

// Named captures
static BrewCapture s_captures[BREW_CAPTURE_MAX];
static uint8_t     s_capture_count = 0;

// Deferred save — brew_stop() runs on the LVGL task whose stack lives in
// PSRAM.  Flash I/O (LittleFS + NVS) disables the cache, making a PSRAM
// stack inaccessible and triggering an assert.  We set a flag here and
// perform the actual save in brew_tick(), which runs on the main Arduino
// loop task (internal-RAM stack, flash-safe).
static bool  s_save_pending = false;
static float s_save_weight  = 0.0f;

// ============================================================================
// Side effect dispatcher (bitmask)
// ============================================================================

static void emit_marker(const char* label) {
    if (s_marker_count >= BREW_MARKER_MAX) return;
    BrewMarker& m = s_markers[s_marker_count++];
    m.sample_index = s_series_count > 0 ? s_series_count - 1 : 0;
    strlcpy(m.label, label, sizeof(m.label));
    LOGD(TAG, "Marker[%u]: t=%u '%s'", (unsigned)(s_marker_count - 1),
         (unsigned)m.sample_index, label);
}

static void emit_capture(const BrewStage* stage) {
    if (s_capture_count >= BREW_CAPTURE_MAX) return;
    if (!stage->capture_key[0]) return;  // no key configured
    BrewCapture& c = s_captures[s_capture_count++];
    strlcpy(c.key, stage->capture_key, sizeof(c.key));
    strlcpy(c.label, stage->capture_label, sizeof(c.label));
    strlcpy(c.unit, stage->capture_unit, sizeof(c.unit));
    c.value = hx711_get_weight();
    LOGI(TAG, "Capture[%u]: %s=%.1f %s", (unsigned)(s_capture_count - 1),
         c.key, c.value, c.unit);
}

static void dispatch_effects(BrewEffects effects, const BrewStage* stage) {
    if (effects == EFFECT_NONE) return;
    if (effects & EFFECT_TARE) {
        hx711_request_tare_no_persist();
        LOGD(TAG, "Effect: tare");
    }
    if (effects & EFFECT_CAPTURE_DOSE) {
        s_dose_weight = hx711_get_weight();
        LOGI(TAG, "Effect: capture_dose=%.1f g", s_dose_weight);
    }
    if (effects & EFFECT_CAPTURE_WEIGHT) {
        emit_capture(stage);
    }
    if (effects & EFFECT_MARKER) {
        if (s_timer_running) {
            emit_marker(stage->name);
        }
    }
#if HAS_AUDIO
    if (effects & EFFECT_BEEP) {
        audio_beep(nullptr, 0);  // default beep
        LOGD(TAG, "Effect: beep");
    }
#endif
}

// ============================================================================
// Internal helpers
// ============================================================================

static const BrewStage* current_stage() {
    if (!s_template || s_stage_index >= s_template->stage_count) return nullptr;
    return &s_template->stages[s_stage_index];
}

static void ensure_series_buffer() {
    if (!s_series) {
        s_series = (BrewSample*)heap_caps_malloc(
            BREW_SERIES_MAX_SAMPLES * sizeof(BrewSample),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_series) {
            LOGW(TAG, "PSRAM alloc failed for series buffer");
        }
    }
}

static void enter_stage(uint8_t index) {
    s_stage_index = index;
    const BrewStage* stage = current_stage();
    if (!stage) return;
    s_stage_enter_ms = millis();
    LOGI(TAG, "Enter stage[%u] '%s'", (unsigned)index, stage->name);

    // Auto-emit marker on stage transitions while timer is running.
    // NOTE: At AUTO_WEIGHT transitions, brew_tick() emits a marker for the
    // trigger stage just before calling enter_stage(), so two markers may
    // land at the same sample_index with different labels.  This is expected.
    if (s_timer_running) {
        emit_marker(stage->name);
    }

    dispatch_effects(stage->on_enter, stage);
}

static void record_sample() {
    if (!s_series || s_series_count >= BREW_SERIES_MAX_SAMPLES) return;
    s_series[s_series_count].weight = hx711_get_weight();
    s_series[s_series_count].flow   = hx711_get_flow_rate();
    s_series_count++;
}

// ============================================================================
// Control API
// ============================================================================

void brew_start(const char* template_name) {
    const BrewTemplate* tpl = brew_template_find(template_name);
    if (!tpl) {
        LOGW(TAG, "brew_start: template '%s' not found, using free_pour",
             template_name ? template_name : "");
        tpl = brew_template_find("free_pour");
    }
    if (!tpl) {
        LOGE(TAG, "brew_start: no templates registered");
        return;
    }

    // Reset any previous brew
    brew_free_series();
    s_elapsed_ms     = 0;
    s_start_ms       = 0;
    s_timer_running  = false;
    s_dose_weight    = 0.0f;
    s_save_pending   = false;
    s_last_sample_ms = 0;
    s_stage_enter_ms = 0;
    s_marker_count   = 0;
    s_capture_count  = 0;

    s_template      = tpl;
    s_last_template = tpl;  // remember for [brew:next_label] while idle
    s_stage_index   = 0;
    s_phase         = BREW_ACTIVE;

    LOGI(TAG, "Start: template='%s', taring scale", tpl->name);
    hx711_request_tare_no_persist();
    enter_stage(0);
}

void brew_next() {
    if (s_phase != BREW_ACTIVE) return;
    const BrewStage* stage = current_stage();
    if (!stage || stage->type != STAGE_MANUAL) return;

    LOGI(TAG, "Next: leaving stage[%u] '%s'", (unsigned)s_stage_index, stage->name);
    dispatch_effects(stage->on_exit, stage);

    uint8_t next_index = s_stage_index + 1;
    if (next_index >= s_template->stage_count) {
        // No more stages — treat as stop
        brew_stop();
        return;
    }
    enter_stage(next_index);
}

void brew_stop() {
    if (s_phase != BREW_ACTIVE) return;

    if (s_timer_running) {
        s_elapsed_ms = millis() - s_start_ms;
        LOGI(TAG, "Stop: elapsed=%lu ms, weight=%.1f g",
             (unsigned long)s_elapsed_ms, hx711_get_weight());
        s_save_weight  = hx711_get_weight();
        s_save_pending = true;
    } else {
        LOGI(TAG, "Stop: no timer running (cancelled before pour)");
        brew_free_series();
    }

    s_timer_running = false;
    s_phase         = BREW_DONE;
}

void brew_reset() {
    s_phase          = BREW_IDLE;
    s_template       = nullptr;
    s_stage_index    = 0;
    s_elapsed_ms     = 0;
    s_start_ms       = 0;
    s_timer_running  = false;
    s_dose_weight    = 0.0f;
    s_save_pending   = false;
    s_stage_enter_ms = 0;
    s_marker_count   = 0;
    s_capture_count  = 0;
    brew_free_series();
    LOGI(TAG, "Reset");
}

void brew_advance(const char* template_name) {
    if (s_phase == BREW_IDLE) {
        brew_start(template_name);
        return;
    }
    if (s_phase == BREW_DONE) {
        // Restart the same template seamlessly
        const char* tpl = s_template ? s_template->name : template_name;
        brew_reset();
        brew_start(tpl);
        return;
    }
    if (s_phase == BREW_ACTIVE) {
        const BrewStage* stage = current_stage();
        if (!stage) return;
        if (stage->type == STAGE_MANUAL) {
            brew_next();
        } else if (s_timer_running) {
            // Timer running — treat tap as stop
            brew_stop();
        }
        // AUTO_WEIGHT / AUTO_TIME: no-op on tap; auto-advances
    }
}

// ============================================================================
// Tick — called every sensor poll cycle
// ============================================================================

void brew_tick() {
    // Deferred save — runs on main task (internal RAM stack, flash-safe)
    if (s_save_pending) {
        s_save_pending = false;
        brew_log_save(s_elapsed_ms, s_save_weight,
                      s_template ? s_template->name : "free_pour",
                      s_dose_weight,
                      s_series, s_series_count,
                      s_markers, s_marker_count,
                      s_captures, s_capture_count);
        brew_free_series();
        LOGI(TAG, "Brew saved to flash");
    }

    if (s_phase != BREW_ACTIVE) return;

    const BrewStage* stage = current_stage();
    if (!stage) return;

    // --- AUTO_WEIGHT: detect first pour and start timer ---
    if (stage->type == STAGE_AUTO_WEIGHT) {
        float w = hx711_get_weight();
        if (!s_timer_running && w >= stage->auto_threshold) {
            // First pour detected — start timer and recording
            s_start_ms       = millis();
            s_last_sample_ms = s_start_ms;
            s_timer_running  = true;
            ensure_series_buffer();
            s_series_count   = 0;
            record_sample();  // first sample at t=0
            emit_marker(stage->name);  // opening marker

            LOGI(TAG, "Auto-start: weight=%.1f g", w);

            // Advance to next stage
            uint8_t next_index = s_stage_index + 1;
            if (next_index < s_template->stage_count) {
                enter_stage(next_index);
                stage = current_stage();  // refresh after advance
            }
        }
    }

    // --- AUTO_TIME: advance after duration elapsed ---
    if (stage && stage->type == STAGE_AUTO_TIME && stage->auto_time_ms > 0) {
        uint32_t elapsed_in_stage = millis() - s_stage_enter_ms;
        if (elapsed_in_stage >= stage->auto_time_ms) {
            LOGI(TAG, "Auto-time: %lu ms elapsed in '%s'",
                 (unsigned long)elapsed_in_stage, stage->name);
            dispatch_effects(stage->on_exit, stage);
            uint8_t next_index = s_stage_index + 1;
            if (next_index < s_template->stage_count) {
                enter_stage(next_index);
            } else {
                brew_stop();
            }
        }
    }

    // --- 1 Hz sampling: record on ALL stages once timer is running ---
    if (s_timer_running) {
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

const char* brew_get_stage_name() {
    if (s_phase == BREW_IDLE) return "Idle";
    if (s_phase == BREW_DONE) return "Done";
    const BrewStage* stage = current_stage();
    return stage ? stage->name : "?";
}

uint32_t brew_get_timer_ms() {
    if (s_timer_running)       return millis() - s_start_ms;
    if (s_phase == BREW_DONE)  return s_elapsed_ms;
    return 0;
}

float brew_get_weight() {
    return hx711_get_weight();
}

float brew_get_flow_rate() {
    return hx711_get_flow_rate();
}

bool brew_is_active() {
    return s_phase == BREW_ACTIVE;
}

const char* brew_get_template_name() {
    if (s_template) return s_template->name;
    return "";
}

float brew_get_dose_weight() {
    return s_dose_weight;
}

float brew_get_water_weight() {
    if (s_phase == BREW_DONE)   return s_save_weight;
    if (s_timer_running)        return hx711_get_weight();
    return 0.0f;
}

float brew_get_stage_weight_target() {
    if (s_phase != BREW_ACTIVE) return 0.0f;
    const BrewStage* stage = current_stage();
    return stage ? stage->target_weight : 0.0f;
}

float brew_get_stage_weight_remaining() {
    float t = brew_get_stage_weight_target();
    if (t <= 0.0f) return 0.0f;
    float diff = t - hx711_get_weight();
    return diff > 0.0f ? diff : 0.0f;
}

float brew_get_stage_flow_target() {
    if (s_phase != BREW_ACTIVE) return 0.0f;
    const BrewStage* stage = current_stage();
    return stage ? stage->target_flow_rate : 0.0f;
}

uint32_t brew_get_stage_time_target_ms() {
    if (s_phase != BREW_ACTIVE) return 0;
    const BrewStage* stage = current_stage();
    if (!stage || stage->auto_time_ms == 0) return 0;
    return stage->auto_time_ms;
}

uint32_t brew_get_stage_time_remaining_ms() {
    if (s_phase != BREW_ACTIVE) return 0;
    const BrewStage* stage = current_stage();
    if (!stage || stage->type != STAGE_AUTO_TIME || stage->auto_time_ms == 0) return 0;
    uint32_t elapsed = millis() - s_stage_enter_ms;
    if (elapsed >= stage->auto_time_ms) return 0;
    return stage->auto_time_ms - elapsed;
}

uint32_t brew_get_stage_time_current_ms() {
    if (s_phase != BREW_ACTIVE || s_stage_enter_ms == 0) return 0;
    return millis() - s_stage_enter_ms;
}

const char* brew_get_display_name() {
    const BrewTemplate* t = s_template ? s_template : s_last_template;
    if (!t) return "";
    if (t->display_name[0]) return t->display_name;
    return t->name;  // fallback to machine name
}

uint8_t brew_get_capture_count() {
    return s_capture_count;
}

const BrewCapture* brew_get_capture(uint8_t index) {
    if (index >= s_capture_count) return nullptr;
    return &s_captures[index];
}

const char* brew_get_instruction() {
    if (s_phase != BREW_ACTIVE) return "";
    const BrewStage* stage = current_stage();
    return (stage && stage->instruction[0]) ? stage->instruction : "";
}

const char* brew_get_next_label() {
    // In IDLE/DONE use the last known template for the label so the button
    // shows "Start V60" / "Brew again" even between brews.
    const BrewTemplate* t = s_template ? s_template : s_last_template;
    if (!t) return "Start";
    if (s_phase == BREW_IDLE) return t->start_label[0] ? t->start_label : "Start";
    if (s_phase == BREW_DONE) return t->done_label[0]  ? t->done_label  : "Brew again";
    const BrewStage* stage = current_stage();
    return (stage && stage->next_label[0]) ? stage->next_label : "Next";
}

void brew_hint_template(const char* template_name) {
    if (!template_name || !template_name[0]) return;
    if (s_phase != BREW_IDLE) return;  // don't override an active brew
    const BrewTemplate* tpl = brew_template_find(template_name);
    if (tpl) s_last_template = tpl;
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
    s_phase         = BREW_IDLE;
    s_template      = nullptr;
    s_stage_index   = 0;
    s_elapsed_ms    = 0;
    s_start_ms      = 0;
    s_timer_running = false;
    s_dose_weight   = 0.0f;
    s_series        = nullptr;
    s_series_count  = 0;
    s_stage_enter_ms = 0;
    s_marker_count  = 0;
    s_capture_count = 0;
    LOGI(TAG, "Init");
}

#endif // HAS_SENSOR_HX711


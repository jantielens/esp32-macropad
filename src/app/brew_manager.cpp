#include "brew_manager.h"

#if HAS_SCALE

#include "brew_log.h"
#include "brew_templates.h"
#include "brew_template_dsl.h"
#include "scale_hal.h"
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

// Countdown beep: precomputed trigger time and one-shot guard
static uint32_t             s_countdown_trigger_ms = 0;  // elapsed-in-stage trigger point
static bool                 s_countdown_fired      = false;
static bool                 s_countdown_done_fired = false;  // fire-once for countdown_done_beep

// Weight proximity cue: counts how many cues have fired this stage
static uint8_t              s_weight_cue_count     = 0;
static bool                 s_weight_done_fired    = false;  // fire-once for weight_done_beep

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

static void emit_marker(const char* label, uint16_t sample_idx) {
    if (s_marker_count >= BREW_MARKER_MAX) return;
    BrewMarker& m = s_markers[s_marker_count++];
    m.sample_index = sample_idx;
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
    c.value = scale_get_weight();
    LOGI(TAG, "Capture[%u]: %s=%.1f %s", (unsigned)(s_capture_count - 1),
         c.key, c.value, c.unit);
}

static void dispatch_effects(BrewEffects effects, const BrewStage* stage) {
    if (effects == EFFECT_NONE) return;
    if (effects & EFFECT_TARE) {
        scale_request_tare_no_persist();
        LOGD(TAG, "Effect: tare");
    }
    if (effects & EFFECT_CAPTURE_DOSE) {
        s_dose_weight = scale_get_weight();
        LOGI(TAG, "Effect: capture_dose=%.1f g", s_dose_weight);
    }
    if (effects & EFFECT_CAPTURE_WEIGHT) {
        emit_capture(stage);
    }
    if (effects & EFFECT_MARKER) {
        if (s_timer_running) {
            emit_marker(stage->name, s_series_count);
        }
    }
#if HAS_AUDIO
    if (effects & EFFECT_BEEP) {
        const char* pattern = (stage && stage->beep_pattern[0]) ? stage->beep_pattern : nullptr;
        audio_beep(pattern, 0);
        LOGD(TAG, "Effect: beep%s%s", pattern ? " pattern=" : "", pattern ? pattern : "");
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

    // Precompute countdown beep trigger for auto_time stages
    s_countdown_fired = false;
    s_countdown_done_fired = false;
    s_countdown_trigger_ms = 0;
    s_weight_cue_count = 0;
    s_weight_done_fired = false;
#if HAS_AUDIO
    if (stage->type == STAGE_AUTO_TIME && stage->auto_time_ms > 0
        && stage->countdown_beep[0]) {
        uint32_t pattern_dur = brew_dsl_beep_duration_ms(stage->countdown_beep);
        if (pattern_dur > 0 && pattern_dur < stage->auto_time_ms) {
            s_countdown_trigger_ms = stage->auto_time_ms - pattern_dur;
            LOGD(TAG, "Countdown beep at %lu ms (pattern %lu ms)",
                 (unsigned long)s_countdown_trigger_ms, (unsigned long)pattern_dur);
        }
    }
#endif

    // Auto-emit marker on stage transitions while timer is running,
    // unless the stage already has an explicit MARKER effect in on_enter
    // (which would create a duplicate).
    if (s_timer_running && !(stage->on_enter & EFFECT_MARKER)) {
        emit_marker(stage->name, s_series_count);
    }

    dispatch_effects(stage->on_enter, stage);
}

static void record_sample() {
    if (!s_series || s_series_count >= BREW_SERIES_MAX_SAMPLES) return;
    s_series[s_series_count].weight = scale_get_weight_ema();  // smooth weight for brew data (no dead-band staircase)
    s_series[s_series_count].flow   = scale_get_flow_rate();
    s_series_count++;
}

// ============================================================================
// Control API
// ============================================================================

void brew_start(const char* template_name) {
    const BrewTemplate* tpl = (template_name && template_name[0])
                              ? brew_template_find(template_name) : nullptr;
    if (!tpl) tpl = s_last_template;  // fall back to hinted template
    if (!tpl) tpl = brew_template_find("free_pour");
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
    scale_request_tare_no_persist();
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
             (unsigned long)s_elapsed_ms, scale_get_weight());
        s_save_weight  = scale_get_weight();
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
                      s_template,
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
        float w = scale_get_weight();
        if (!s_timer_running && w >= stage->auto_threshold) {
            // First pour detected — start timer and recording
            s_start_ms       = millis();
            s_last_sample_ms = s_start_ms;
            s_timer_running  = true;
            ensure_series_buffer();
            s_series_count   = 0;

            LOGI(TAG, "Auto-start: weight=%.1f g", w);

            // Advance to next stage (before first sample so markers
            // from on_enter effects land at index 0)
            uint8_t next_index = s_stage_index + 1;
            if (next_index < s_template->stage_count) {
                enter_stage(next_index);
                stage = current_stage();  // refresh after advance
            }

            record_sample();  // first sample at t=0
        }
    }

    // --- AUTO_TIME: advance after duration elapsed ---
    if (stage && stage->type == STAGE_AUTO_TIME && stage->auto_time_ms > 0) {
        uint32_t elapsed_in_stage = millis() - s_stage_enter_ms;

#if HAS_AUDIO
        // Fire countdown beep pattern aligned to end at stage expiry
        if (!s_countdown_fired && s_countdown_trigger_ms > 0
            && elapsed_in_stage >= s_countdown_trigger_ms) {
            s_countdown_fired = true;
            audio_beep(stage->countdown_beep, 0);
            LOGD(TAG, "Countdown beep fired at %lu ms", (unsigned long)elapsed_in_stage);
        }
#endif

        if (elapsed_in_stage >= stage->auto_time_ms) {
            LOGI(TAG, "Auto-time: %lu ms elapsed in '%s'",
                 (unsigned long)elapsed_in_stage, stage->name);
#if HAS_AUDIO
            if (!s_countdown_done_fired && stage->countdown_done_beep[0]) {
                s_countdown_done_fired = true;
                audio_beep(stage->countdown_done_beep, 0);
                LOGD(TAG, "Countdown done beep fired");
            }
#endif
            dispatch_effects(stage->on_exit, stage);
            uint8_t next_index = s_stage_index + 1;
            if (next_index < s_template->stage_count) {
                enter_stage(next_index);
            } else {
                brew_stop();
            }
            // Return immediately — the local `stage` pointer is now stale.
            // Without this, weight cue/done checks below would run against
            // the OLD stage's target_weight, spuriously firing and poisoning
            // s_weight_done_fired for the new stage.
            return;
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

#if HAS_AUDIO
    // --- Weight proximity cue ---
    if (stage && stage->weight_cue_g > 0.0f && stage->target_weight > 0.0f
        && s_weight_cue_count < stage->weight_cue_times) {
        float next_threshold_g = stage->weight_cue_g * (stage->weight_cue_times - s_weight_cue_count);
        float remaining = stage->target_weight - scale_get_weight();
        if (remaining <= next_threshold_g && remaining > 0.0f) {
            s_weight_cue_count++;
            const char* pattern = stage->weight_cue_beep[0] ? stage->weight_cue_beep : nullptr;
            audio_beep(pattern, 0);
            LOGD(TAG, "Weight cue %u/%u at %.1f g remaining",
                 (unsigned)s_weight_cue_count, (unsigned)stage->weight_cue_times, remaining);
        }
    }
    // --- Weight done beep: fire once when weight reaches target ---
    if (stage && stage->weight_done_beep[0] && stage->target_weight > 0.0f
        && !s_weight_done_fired) {
        float w = scale_get_weight();
        if (w >= stage->target_weight) {
            s_weight_done_fired = true;
            audio_beep(stage->weight_done_beep, 0);
            LOGD(TAG, "Weight done beep at %.1f g (target %.1f)", w, stage->target_weight);
        }
    }
#endif
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
    return scale_get_weight();
}

float brew_get_flow_rate() {
    return scale_get_flow_rate();
}

bool brew_is_active() {
    return s_phase == BREW_ACTIVE;
}

const char* brew_get_template_name() {
    if (s_template) return s_template->name;
    if (s_last_template) return s_last_template->name;
    return "";
}

float brew_get_dose_weight() {
    return s_dose_weight;
}

float brew_get_water_weight() {
    if (s_phase == BREW_DONE)   return s_save_weight;
    if (s_timer_running)        return scale_get_weight();
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
    float diff = t - scale_get_weight();
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

#endif // HAS_SCALE


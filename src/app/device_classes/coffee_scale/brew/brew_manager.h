#pragma once

#include "board_config.h"

#if HAS_SCALE

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// Brew Manager — template-driven state machine with auto-start timer
// ============================================================================
// A BrewTemplate defines an ordered array of BrewStages. The manager holds a
// pointer to the active template and a stage index; it doesn't care whether
// the template is a built-in (static const) or a heap-allocated one loaded
// from the filesystem.
//
// Meta-phases:
//   BREW_IDLE   — no template loaded
//   BREW_ACTIVE — template running; brew_get_stage_name() returns the current
//                 stage name (e.g. "Dosing", "Ready", "Brewing")
//   BREW_DONE   — brew finished, timer frozen, report saved
//
// Stage types control how a stage advances:
//   STAGE_MANUAL      — user calls brew_next() to advance
//   STAGE_AUTO_WEIGHT — auto-advances when weight ≥ auto_threshold
//   STAGE_AUTO_TIME   — auto-advances after auto_time_ms elapsed in this stage
//
// Side effects (on_enter / on_exit) are bitmasks — multiple effects can fire
// on a single transition.  Effects are serializable as strings for future
// config-driven templates.
//
// Recording: the series buffer is allocated when the brew timer starts
// (first pour detected on any AUTO_WEIGHT stage).  From that point onward,
// 1 Hz weight+flow samples are captured regardless of stage type.  A marker
// is emitted automatically whenever a stage transition occurs while the timer
// is running.

// ---- Stage behaviour ----

enum BrewStageType : uint8_t {
    STAGE_MANUAL      = 0,
    STAGE_AUTO_WEIGHT = 1,
    STAGE_AUTO_TIME   = 2,  // auto-advance after auto_time_ms
};

// Side-effect bitmask flags — combine with bitwise OR.
// Example: EFFECT_TARE | EFFECT_BEEP
#define EFFECT_NONE            0x00
#define EFFECT_TARE            0x01  // tare the scale
#define EFFECT_CAPTURE_DOSE    0x02  // capture weight → built-in "dose" slot (legacy)
#define EFFECT_BEEP            0x04  // play a short audio cue
#define EFFECT_CAPTURE_WEIGHT  0x08  // capture weight → named capture slot (uses capture_key/label/unit)
#define EFFECT_MARKER          0x10  // emit a named marker into the series timeline

typedef uint8_t BrewEffects;  // bitmask of EFFECT_* flags

// Maximum named capture slots per brew
#define BREW_CAPTURE_MAX   8
// Maximum markers per brew
#define BREW_MARKER_MAX    16

struct BrewCapture {
    char  key[16];    // field key in brew log, e.g. "bloom_water"
    char  label[24];  // display label, e.g. "Bloom Water"
    char  unit[8];    // unit string, e.g. "g"
    float value;
};

struct BrewMarker {
    uint16_t sample_index;  // index into series (seconds since timer start)
    char     label[24];     // marker label, e.g. "Bloom wait"
};

struct BrewStage {
    char           name[24];         // display name shown via [brew:stage]
    char           instruction[128]; // user-facing guidance shown via [brew:instruction]
    char           next_label[48];   // label for the advance button on this stage
    BrewStageType  type;
    BrewEffects    on_enter;         // bitmask fired when entering stage
    BrewEffects    on_exit;          // bitmask fired when leaving stage
    float          auto_threshold;   // g above tare; used by AUTO_WEIGHT
    float          target_weight;    // guidance target weight for this stage (0 = none)
    float          target_flow_rate; // guidance flow rate target g/s (0 = none)
    uint32_t       auto_time_ms;     // duration for AUTO_TIME stages (0 = unused)
    // Custom beep pattern (audio DSL, e.g. "600:40 40 600:40"); empty = default beep
    char           beep_pattern[48];
    // Countdown beep pattern (audio DSL) played before auto_time stage ends;
    // pattern duration is calculated and aligned to end exactly at stage expiry.
    char           countdown_beep[48];
    // Countdown done beep — played once when auto_time countdown reaches zero.
    char           countdown_done_beep[48];
    // Weight proximity cue: beep when within weight_cue_g of target_weight.
    // weight_cue_times > 1 fires evenly-spaced cues at N×g, (N-1)×g, … 1×g.
    float          weight_cue_g;     // gram interval (0 = disabled)
    uint8_t        weight_cue_times; // number of cues (default 1)
    char           weight_cue_beep[48]; // beep pattern DSL (empty = default beep)
    // Weight done beep — played once when weight >= target_weight.
    char           weight_done_beep[48];
    // Fields used when EFFECT_CAPTURE_WEIGHT is set (in on_enter or on_exit):
    char           capture_key[16];  // key for named capture, e.g. "bloom_water"
    char           capture_label[24];// display label, e.g. "Bloom Water"
    char           capture_unit[8];  // unit string, e.g. "g"
};

struct BrewTemplate {
    char             name[24];        // machine name: "v60", "rao_v60"
    char             display_name[48];// human-friendly name for UI
    char             description[128];// template description / help text
    char             start_label[48]; // advance button label when Idle (before starting)
    char             done_label[48];  // advance button label when Done (restart prompt)
    const BrewStage* stages;
    uint8_t          stage_count;
    bool             is_dynamic;      // true = heap-allocated; freed on brew_reset/unregister
};

// ---- Meta-phase ----

enum BrewPhase : uint8_t {
    BREW_IDLE   = 0,
    BREW_ACTIVE = 1,   // template running; stage index tracks position
    BREW_DONE   = 2,   // brew finished (timer frozen)
};

// Maximum series samples (1 Hz recording, 600 = 10 minutes)
#define BREW_SERIES_MAX_SAMPLES  600

// One time-series sample (weight + flow at a given second)
struct BrewSample {
    float weight;
    float flow;
};

// ---- Control API (called from action_dispatch) ----

// Start brewing with the named template (nullptr or "" → "free_pour").
// Resets any in-progress brew. Safe to call from any state.
void brew_start(const char* template_name = nullptr);

// Advance the current MANUAL stage: fires on_exit, moves to next stage,
// fires on_enter. No-op if the current stage is not MANUAL or brew is not
// ACTIVE.
void brew_next();

// Smart single-button advance — does the right thing at every state:
//   Idle   → brew_start(template_name)
//   Manual → brew_next()
//   Timer running → brew_stop()
//   Auto-weight / Auto-time → no-op (auto-advances)
//   Done   → brew_reset() + brew_start(same template)
void brew_advance(const char* template_name = nullptr);

// Freeze timer, enter DONE. No-op if not ACTIVE.
void brew_stop();

// Clear all state, enter IDLE. Safe to call from any state.
void brew_reset();

// ---- Tick (called from sensor loop or main loop) ----

// Advance state machine. Must be called periodically (each sensor poll).
void brew_tick();

// ---- Query API (called from brew_binding resolver) ----

// Returns stage name when ACTIVE, "Idle" or "Done" otherwise.
const char* brew_get_stage_name();
uint32_t    brew_get_timer_ms();         // elapsed ms (0 when idle/not yet brewing)
float       brew_get_weight();           // current weight from HX711
float       brew_get_flow_rate();        // current flow rate from HX711
bool        brew_is_active();            // true if BREW_ACTIVE
const char* brew_get_template_name();    // active template name, or "" when idle
float       brew_get_dose_weight();      // captured dose weight (0 if not captured)
float       brew_get_water_weight();     // water poured: live while timer running, frozen after Done (0 otherwise)
float       brew_get_stage_weight_target();     // current stage target weight (0 if none)
float       brew_get_stage_weight_remaining();  // max(0, target - weight) grams left
float       brew_get_stage_flow_target();        // current stage target flow rate g/s (0 if none)
uint32_t    brew_get_stage_time_target_ms();     // current stage auto_time_ms (0 if not AUTO_TIME)
uint32_t    brew_get_stage_time_remaining_ms(); // remaining ms for AUTO_TIME stage (0 otherwise)
uint32_t    brew_get_stage_time_current_ms();   // ms since current stage entered (0 when idle)
const char* brew_get_display_name();     // template display_name (falls back to name)
// Current stage instruction text, or "" when Idle/Done. Use [brew:instruction|fallback].
const char* brew_get_instruction();
// Label for the single advance button — changes with each stage.
const char* brew_get_next_label();
// Access captured data points (for brew_binding)
uint8_t     brew_get_capture_count();
const BrewCapture* brew_get_capture(uint8_t index);
// Meta-phase, stage count, and stage index for widget data bindings
BrewPhase   brew_get_phase();
uint8_t     brew_get_stage_count();     // 0 when no template loaded
uint8_t     brew_get_stage_index();     // 0-based index of current stage (0 when idle/done)
const BrewStage* brew_get_stage(uint8_t index);  // stage by index, nullptr if out of range
// Pre-prime the idle label so [brew:next_label] shows the template's
// start_label before the first tap. Call from action_dispatch when handling
// advance:<name> — even before the brew starts.
void brew_hint_template(const char* template_name);

// Format timer into buffer. fmt: "mm:ss", "hh:mm:ss", "ss", "mm:ss.d"
// Returns number of chars written (excl NUL).
int brew_format_timer(const char* fmt, char* out, size_t out_len);

// ---- Series access ----

// Free the series buffer (called after brew_log_save completes).
void brew_free_series();

// ---- Init ----

void brew_manager_init();

#endif // HAS_SCALE

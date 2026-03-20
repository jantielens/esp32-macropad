#pragma once

#include "board_config.h"

#if HAS_SENSOR_HX711

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
//   BREW_ACTIVE — template running; brew_get_phase_name() returns the current
//                 stage name (e.g. "Dosing", "Ready", "Brewing")
//   BREW_DONE   — brew finished, timer frozen, report saved
//
// Stage types control how a stage advances:
//   STAGE_MANUAL      — user calls brew_next() to advance
//   STAGE_AUTO_WEIGHT — auto-advances when weight ≥ auto_threshold
//   STAGE_RECORDING   — like AUTO_WEIGHT but also records weight/flow series
//                       (only one RECORDING stage per template, must be last)
//
// Side effects (on_enter / on_exit) are named enums, not function pointers,
// so they can be serialized to/from JSON for future config-driven templates.

// ---- Stage behaviour ----

enum BrewStageType : uint8_t {
    STAGE_MANUAL      = 0,
    STAGE_AUTO_WEIGHT = 1,
    STAGE_RECORDING   = 2,
};

// Named side effects — serializable as strings for future JSON templates.
// "none"=0, "tare"=1, "capture_dose"=2
enum BrewSideEffect : uint8_t {
    EFFECT_NONE         = 0,
    EFFECT_TARE         = 1,
    EFFECT_CAPTURE_DOSE = 2,
};

struct BrewStage {
    char           name[24];         // display name shown via [brew:phase]
    char           instruction[128]; // user-facing guidance shown via [brew:instruction]
    char           next_label[48];   // label for the advance button on this stage
    BrewStageType  type;
    BrewSideEffect on_enter;         // side effect fired when entering stage
    BrewSideEffect on_exit;          // side effect fired when leaving stage (brew_next)
    float          auto_threshold;   // g above tare; used by AUTO_WEIGHT and RECORDING
};

struct BrewTemplate {
    char             name[24];       // machine name: "v60", "free_pour"
    char             start_label[48]; // advance button label when Idle (before starting)
    char             done_label[48];  // advance button label when Done (restart prompt)
    const BrewStage* stages;
    uint8_t          stage_count;
    bool             is_dynamic;     // true = heap-allocated; freed on brew_reset/unregister
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
//   Recording → brew_stop()
//   Auto-weight → no-op (auto-starts on pour)
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

BrewPhase   brew_get_phase();
// Returns stage name when ACTIVE, "Idle" or "Done" otherwise.
const char* brew_get_phase_name();
uint32_t    brew_get_timer_ms();         // elapsed ms (0 when idle/not yet brewing)
float       brew_get_weight();           // current weight from HX711
float       brew_get_flow_rate();        // current flow rate from HX711
bool        brew_is_active();            // true if BREW_ACTIVE
const char* brew_get_template_name();    // active template name, or "" when idle
float       brew_get_dose_weight();      // captured dose weight (0 if not captured)
float       brew_get_water_weight();     // water poured: live during RECORDING, frozen after Done (0 otherwise)
// Current stage instruction text, or "" when Idle/Done. Use [brew:instruction|fallback].
const char* brew_get_instruction();
// Label for the single advance button — changes with each stage.
const char* brew_get_next_label();
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

#endif // HAS_SENSOR_HX711

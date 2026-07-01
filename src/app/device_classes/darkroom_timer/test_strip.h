#pragma once

#include <stdint.h>
#include "board_config.h"

// ============================================================================
// Test Strip Sequencer — automated f-stop test strip exposure sequence
// ============================================================================
// Compile-time gated by IS_DARKROOM_TIMER (requires HAS_DISPLAY).
//
// Automates the complete test strip workflow: configures base time, step
// interval, and segment count, then exposes each segment in sequence with
// audio cues and inter-segment pauses for mask movement.
//
// Technique: progressive uncover (left-to-right). Start with the paper
// fully masked, reveal one more strip per exposure step.  Segment 1
// receives every exposure and accumulates the most light; the last
// segment receives only its single increment.
//
// State machine:
//   idle ──start──────→ countdown   (initial pre-exposure countdown)
//   countdown ─expire─→ exposing   (relay ON, segment countdown)
//   exposing ─expire──→ pausing    (relay OFF, inter-segment pause)
//   pausing ──expire──→ exposing   (next segment)
//   exposing ─last────→ idle       (final segment done, relay OFF)
//   any ──────cancel──→ idle       (relay OFF, abort)
//
// Audio: pre-expose beep plays in the last 3 seconds of countdown and
// between-segment pause phases.
//
// Binding scheme "strip":
//   [strip:state]            — "idle"/"countdown"/"exposing"/"pausing"
//   [strip:segment]          — current segment number (1-based)
//   [strip:segments]         — total segment count
//   [strip:remaining]        — current phase remaining (seconds, 1 decimal)
//   [strip:remaining;fmt]    — with format override (mm:ss, ss.d, etc.)
//   [strip:elapsed]          — current phase elapsed (seconds, 1 decimal)
//   [strip:elapsed;fmt]      — with format override
//   [strip:seg_time:N]       — cumulative time for segment N (seconds, 1 decimal)
//   [strip:seg_offset:N]     — f-stop offset for segment N (e.g. "-0.3")
//   [strip:seg_inc:N]        — incremental time for segment N (seconds, 1 decimal)
//   [strip:base_time]        — base time setting (seconds, 1 decimal)
//   [strip:step]             — step interval setting (stops, e.g. "0.333")
//   [strip:relay]            — "ON" / "OFF"
//   [strip:progress]         — "3/7"
//   [strip:range]            — "4.0-16.0"
//   [strip:seg_inc]          — incremental time for current segment (seconds)
//   [strip:total_time]       — estimated total sequence time (seconds)
//   [strip:table]            — JSON payload for table widget (segment #, duration, total)
//
// Action type "strip" (command + value as separate fields):
//   command="start"                     — begin sequence from idle
//   command="cancel"                    — abort sequence, relay OFF
//   command="set_base"        value=N   — set base time to N seconds
//   command="adjust_base"     value=N   — adjust base time ±N seconds
//   command="step_up"                   — increase step interval (1/5→1/4→1/3→1/2→1/1)
//   command="step_down"                 — decrease step interval (1/1→1/2→1/3→1/4→1/5)
//   command="adjust_segments" value=N   — adjust segment count ±N (clamped 3–11 odd)
//   command="set_segments"    value=N   — set number of segments (3–11 odd)
//   command="set_countdown"   value=N   — set initial countdown duration (2–10)
//   command="adjust_countdown" value=N  — adjust countdown ±N seconds (clamped 2–10)
//   command="set_pause"       value=N   — set inter-segment pause duration (3–15)
//   command="adjust_pause"    value=N   — adjust pause ±N seconds (clamped 3–15)
//   command="set_tick"        value=on/off — enable/disable per-second exposure tick

// Initialize the test strip subsystem and register the "strip" binding scheme.
// Call once during setup() after binding_template is ready.
void test_strip_init();

// Dispatch a test strip action. Command and value are separate fields
// (e.g. command="set_base", value="8.0").
void test_strip_dispatch(const char* command, const char* value);

// Tick function — call from LVGL render task to drive the state machine.
void test_strip_tick();

#if HAS_MCP
// Maximum segments reported by StripStatus (matches STRIP_MAX_SEGMENTS).
#define STRIP_STATUS_MAX_SEGMENTS 12

// Read-only status snapshot for the MCP get_strip_status tool. The engine's
// fields are plain scalars updated on the main loop; the segment table is kept
// current by every config command (each calls recalculate_segments()) and is
// copied under g_strip_lock, so the read matches the binding-resolver path.
struct StripStatusSegment {
    float cumulative_s;   // cumulative exposure time at this segment
    float incremental_s;  // incremental time for this segment
    float offset_stops;   // f-stop offset from base
};
struct StripStatus {
    uint8_t  phase;            // 0=idle, 1=countdown, 2=exposing, 3=pausing
    int      segment;          // current segment (1-based, 0 when idle)
    int      segment_count;    // total segments
    float    base_time_s;      // base/center exposure time
    float    step_stops;       // step interval in stops
    const char* step_label;    // step label (e.g. "1/3"), static storage
    int      countdown_s;      // initial countdown setting
    int      pause_s;          // inter-segment pause setting
    bool     exposure_tick;    // per-second tick enabled
    uint32_t phase_remaining_ms;
    uint32_t phase_elapsed_ms;
    float    total_time_s;     // estimated total sequence time
    StripStatusSegment segments[STRIP_STATUS_MAX_SEGMENTS];
};
void test_strip_get_status(StripStatus* out);

// Human-readable name for a StripStatus::phase value.
const char* test_strip_phase_str(uint8_t phase);
#endif // HAS_MCP



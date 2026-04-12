#pragma once

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
// Action type "strip" (payload in mqtt_payload field):
//   "start"                  — begin sequence from idle
//   "cancel"                 — abort sequence, relay OFF
//   "set_base:N.N"           — set base time to N.N seconds
//   "add_base:N.N"           — add N.N seconds to base time (positive or negative)
//   "step_up"                — increase step interval (1/5→1/4→1/3→1/2→1/1)
//   "step_down"              — decrease step interval (1/1→1/2→1/3→1/4→1/5)
//   "add_segments:N"         — add N segments (positive or negative, clamped 3–11 odd)
//   "set_segments:N"         — set number of segments (3–11 odd)
//   "set_countdown:N"        — set initial countdown duration (2–10)
//   "set_pause:N"            — set inter-segment pause duration (3–15)
//   "set_tick:on/off"        — enable/disable per-second exposure tick

// Initialize the test strip subsystem and register the "strip" binding scheme.
// Call once during setup() after binding_template is ready.
void test_strip_init();

// Dispatch a test strip action command string (called from action_dispatch).
void test_strip_dispatch(const char* command);

// Tick function — call from LVGL render task to drive the state machine.
void test_strip_tick();

// Deferred operations — call from main loop() on internal-RAM stack.
// Processes pending Shelly HTTP requests.
void test_strip_loop();

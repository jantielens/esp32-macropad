#pragma once

// Register the "shutter" binding scheme with the binding template engine.
// Provides [shutter:key] and [shutter:key;format] tokens for shutter tester data.
//
// Keys are routed by dot-prefix sub-domains:
//
// Measurement keys (flat, no prefix):
//   speed            — Nearest standard speed without trailing 's' (e.g. "1/125")
//   duration_ms      — Measured duration in ms (e.g. "8.1")
//   deviation        — Deviation from nominal (e.g. "+3%")
//   deviation_abs    — Absolute deviation in % (numeric, for gauge widget)
//   deviation_stops  — Deviation in stops (numeric)
//   verdict          — "pass", "warning", or "fail"
//   spread           — Spread across sensors in % (e.g. "2.1")
//   capping_gradient  — Capping gradient in stops/mm (3 decimal places, or empty if unavailable)\n//   capping_frame_stops — Estimated full-frame capping in stops (gradient × 43.27 mm, 2 dp, or empty)
//   sensor_N_ms      — Sensor N duration in ms (N=1..sensor_count, up to SHUTTER_SENSOR_MAX)
//   sensor_N_valid   — "true"/"false" per-sensor validity (N=1..sensor_count)
//   sensor_N_depth   — Sensor N pulse depth (baseline-min ADC counts)
//   sensor_N_snr     — Sensor N signal-to-noise ratio
//   sensor_N_saturated — "true"/"false" sensor saturation flag
//   count            — Total measurements since boot
//   capture_id       — Change-detection token (same as count)
//   available        — "true" or "false"
//   history_json     — JSON array of recent measurements (for table widget)
//   target_speed     — Currently locked target speed label (e.g. "1/1000s"), or "---"
//   speed_locked     — "true" if target speed is locked, "false" otherwise
//
// Session keys (session.* prefix):
//   session.active   — "true" / "false"
//   session.count    — Number of measurements in current session
//   session.id       — Current session ID string (e.g. "sess_42")
//   session.verdict  — Worst verdict across active session measurements ("pass"/"warning"/"fail", or "" if inactive)
//
// Alignment keys (align.* prefix):
//   align.active     — "true" / "false"
//   align.sN_pct     — Sensor N illumination percentage (0-100), N=1..9
//   align.sN_raw     — Sensor N raw ADC value (decimated average), N=1..9
//   align.spread     — Spread percentage across sensors (max_pct - min_pct)
//   align.status     — 3-tier status: "ready", "usable", "not-ready"
//   align.hint       — Short human-readable hint (empty when status is "ready")
//   align.sensor_count — Number of active sensors
//
// Call once during setup(), after binding_template_init().
void shutter_binding_init();

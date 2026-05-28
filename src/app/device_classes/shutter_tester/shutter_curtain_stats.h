#pragma once

#include <stdint.h>
#include <stddef.h>

// ============================================================================
// ShutterCurtainStats — per-sensor curtain edge timing
// ============================================================================
//
// Computed from the full-resolution raw waveform before downsampling.
// Stored in session JSON so the portal can display curtain metrics without
// re-deriving them from the (lower-resolution) stored waveform.
//
// Validity model (two-tier):
//
//   `edges_detected` — true when the 10%/50%/90% threshold crossings on both
//     the opening and closing edges were located. When false, no timing fields
//     are populated and the entire stats object should be omitted from output.
//
//   `valid` — true when `edges_detected` AND the measurement is *physically
//     meaningful as a curtain balance measurement* (slit-mode regime, no
//     sensor-recovery artifact). When false but `edges_detected` is true,
//     the raw timing fields (`curtain1_ms`, `curtain2_ms`, `dwell_ms`) are
//     still populated for diagnostic transparency but `curtain_ratio` is
//     not physically meaningful and should be suppressed by consumers.
//
// Gates (applied after edge detection succeeds):
//
//   #1 Full-open mode: dwell_ms > MIN_SLIT_DWELL_RATIO * max(c1, c2)
//      → both curtains travel independently with no slit overlap; the
//        "ratio" measures sensor electronics, not shutter mechanics.
//
//   #2 Close-edge recovery tail: (close10 - close90) > MAX_CLOSE_EDGE_WIDTH_RATIO
//      * (open90 - open10) → close edge scan ran past the curtain transit
//      into the photodiode/TIA recovery tail; curtain2_ms reflects sensor
//      recovery, not curtain mechanics.
struct ShutterCurtainStats {
    bool  edges_detected;   // true ⇒ all 6 threshold crossings located
    bool  valid;            // true ⇒ edges_detected AND gates passed (curtain_ratio meaningful)
    float curtain1_ms;      // 1st curtain travel time (10% → 90% excursion)
    float dwell_ms;         // Full-open dwell between 1st and 2nd curtain
    float curtain2_ms;      // 2nd curtain travel time (90% → 10% excursion)
    float curtain_ratio;    // curtain2_ms / curtain1_ms (0 if curtain1_ms == 0)
    float curtain1_start_frac; // Fractional position of 1st curtain opening start [0..1]
    float curtain1_end_frac;   // Fractional position of 1st curtain opening end   [0..1]
    float curtain1_mid_frac;   // 50% (steepest) crossing of 1st curtain   [0..1]
    float curtain2_start_frac; // Fractional position of 2nd curtain closing start [0..1]
    float curtain2_end_frac;   // Fractional position of 2nd curtain closing end   [0..1]
    float curtain2_mid_frac;   // 50% (steepest) crossing of 2nd curtain   [0..1]
};

// Validity gate constants. Tunable defaults selected from sess_91 field data:
//   - At 1/4s and 1s on a 4-sensor capture, Sensor 4 produced curtain2_ms
//     values 29-41× larger than curtain1_ms due to a slow recovery tail.
//   - At 1/60s and faster, all sensors stay within ratio 0.7-1.1.
//   - Gate #1 ratio of 4 catches everything from 1/15s and slower while
//     leaving slit-mode speeds (1/60s and faster on this rig) intact.
//   - Gate #2 ratio of 3 catches the recovery-tail artifact at intermediate
//     speeds where dwell isn't yet long enough to trigger Gate #1.
static constexpr float MIN_SLIT_DWELL_RATIO        = 4.0f;
static constexpr float MAX_CLOSE_EDGE_WIDTH_RATIO  = 3.0f;

// Compute curtain edge statistics for a single sensor using 10%/90% adaptive
// thresholds on the raw ADC waveform. Signal is inverted: baseline is HIGH,
// trough (peak light) is LOW.
//
// IMPORTANT — baseline contract:
//   `pre_pulse_baseline` MUST be the true pre-pulse baseline computed from
//   raw samples *before* the pulse begins (e.g. ShutterSensorResult.baseline_adc).
//   Do NOT derive baseline from the first samples of `samples` here: the slice
//   passed in is pulse-centered, so at fast shutter speeds those leading
//   samples already lie inside the pulse and would contaminate every
//   downstream threshold (swing / thr10 / thr50 / thr90) and bias every
//   reported curtain timing. Caller is responsible for sourcing an honest
//   pre-pulse baseline.
//
// Edge timing accuracy: linear sub-sample interpolation eliminates ADC
// sample-period quantization jitter. thr50 crossings are used for fractional
// positions because the curtain transition is steepest there, minimising
// signal-noise → time-noise conversion. 10%/90% crossings remain the source
// of truth for the curtain edge widths and the dwell definition.
ShutterCurtainStats compute_curtain_stats(const uint16_t* samples,
                                          uint32_t count,
                                          float sample_rate_hz,
                                          float pre_pulse_baseline);

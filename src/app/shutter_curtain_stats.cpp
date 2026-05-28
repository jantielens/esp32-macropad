// ============================================================================
// Curtain edge statistics — full-resolution waveform analysis
// ============================================================================
//
// See shutter_curtain_stats.h for the public contract, the two-tier validity
// model (edges_detected vs valid), and the rationale for the two physical-
// validity gates applied after edge detection succeeds.
//
// This translation unit is intentionally dependency-free (no FreeRTOS,
// LittleFS, LVGL, Arduino) so it can be unit-tested on the host.

#include "shutter_curtain_stats.h"

#include <math.h>

ShutterCurtainStats compute_curtain_stats(const uint16_t* samples,
                                          uint32_t count,
                                          float sample_rate_hz,
                                          float pre_pulse_baseline) {
    ShutterCurtainStats stats = {};
    stats.edges_detected = false;
    stats.valid          = false;

    if (!samples || count < 4 || sample_rate_hz <= 0.0f) return stats;
    if (pre_pulse_baseline <= 0.0f) return stats;  // honest baseline required

    float baseline = pre_pulse_baseline;

    // Find trough (minimum ADC) across the entire waveform.
    uint16_t min_adc = samples[0];
    for (uint32_t i = 1; i < count; i++) {
        if (samples[i] < min_adc) min_adc = samples[i];
    }

    float swing = baseline - (float)min_adc;
    if (swing < 50.0f) return stats;  // not enough excursion

    // 10%/50%/90% thresholds (subtractive — signal drops during exposure).
    // 10% / 90% bracket the curtain edge for width measurement.
    // 50% is the steepest crossing point — used for fractional position
    // outputs to minimize signal-noise→time-noise conversion.
    float thr10 = baseline - 0.10f * swing;
    float thr50 = baseline - 0.50f * swing;
    float thr90 = baseline - 0.90f * swing;

    // --- Open edge: detect 10%, 50%, 90% crossings in order ---
    float open10 = -1.0f, open50 = -1.0f, open90 = -1.0f;
    for (uint32_t i = 1; i < count; i++) {
        float y0 = (float)samples[i - 1], y1 = (float)samples[i];
        if (open10 < 0.0f && y0 > thr10 && y1 <= thr10) {
            open10 = (y0 != y1) ? ((float)(i - 1) + (y0 - thr10) / (y0 - y1)) : (float)i;
        }
        if (open10 >= 0.0f && open50 < 0.0f && y0 > thr50 && y1 <= thr50) {
            open50 = (y0 != y1) ? ((float)(i - 1) + (y0 - thr50) / (y0 - y1)) : (float)i;
        }
        if (open50 >= 0.0f && open90 < 0.0f && y0 > thr90 && y1 <= thr90) {
            open90 = (y0 != y1) ? ((float)(i - 1) + (y0 - thr90) / (y0 - y1)) : (float)i;
            break;
        }
    }
    if (open10 < 0.0f || open50 < 0.0f || open90 < 0.0f) return stats;

    // --- Find trough index (for scanning close edge from there) ---
    uint32_t trough_idx = 0;
    for (uint32_t i = 1; i < count; i++) {
        if (samples[i] < samples[trough_idx]) trough_idx = i;
    }

    // --- Close edge: rising signal crosses 90%, 50%, 10% in order ---
    float close90 = -1.0f, close50 = -1.0f, close10 = -1.0f;
    for (uint32_t i = trough_idx + 1; i < count; i++) {
        float y0 = (float)samples[i - 1], y1 = (float)samples[i];
        if (close90 < 0.0f && y0 < thr90 && y1 >= thr90) {
            close90 = (y0 != y1) ? ((float)(i - 1) + (thr90 - y0) / (y1 - y0)) : (float)i;
        }
        if (close90 >= 0.0f && close50 < 0.0f && y0 < thr50 && y1 >= thr50) {
            close50 = (y0 != y1) ? ((float)(i - 1) + (thr50 - y0) / (y1 - y0)) : (float)i;
        }
        if (close50 >= 0.0f && close10 < 0.0f && y0 < thr10 && y1 >= thr10) {
            close10 = (y0 != y1) ? ((float)(i - 1) + (thr10 - y0) / (y1 - y0)) : (float)i;
            break;
        }
    }
    if (close90 < 0.0f || close50 < 0.0f || close10 < 0.0f) return stats;

    // Edges detected — populate all timing fields regardless of validity gates
    // so consumers can show diagnostic c1/dwell/c2 even in full-open mode.
    stats.edges_detected = true;

    float ms_per_sample = 1000.0f / sample_rate_hz;
    stats.curtain1_ms = (open90 - open10) * ms_per_sample;
    stats.dwell_ms    = (close90 - open90) * ms_per_sample;
    if (stats.dwell_ms < 0.0f) stats.dwell_ms = 0.0f;
    stats.curtain2_ms = (close10 - close90) * ms_per_sample;
    stats.curtain_ratio = (stats.curtain1_ms > 0.0f)
                              ? (stats.curtain2_ms / stats.curtain1_ms)
                              : 0.0f;

    // Fractional positions [0..1] within the full raw waveform.
    // start/end use 10%/90% (canonical edge bounds, preserve historical
    // semantics and edge-width calculations downstream).
    // mid uses 50% — steepest crossing point, lowest signal-noise to
    // time-noise conversion. Use this for geometric/spatial calculations
    // (exposure simulation, residual ticks, capping geometry).
    float fcount = (float)(count - 1);
    stats.curtain1_start_frac = open10  / fcount;
    stats.curtain1_end_frac   = open90  / fcount;
    stats.curtain1_mid_frac   = open50  / fcount;
    stats.curtain2_start_frac = close90 / fcount;
    stats.curtain2_end_frac   = close10 / fcount;
    stats.curtain2_mid_frac   = close50 / fcount;

    // --- Physical-validity gates (see shutter_curtain_stats.h for rationale) ---

    // Gate #1: full-open mode — dwell dominates over curtain transit, so the
    // two curtains travel independently and "balance" is meaningless.
    float max_curtain_transit = fmaxf(stats.curtain1_ms, stats.curtain2_ms);
    if (stats.dwell_ms > MIN_SLIT_DWELL_RATIO * max_curtain_transit) {
        return stats;  // edges_detected=true, valid stays false
    }

    // Gate #2: close-edge recovery tail — close-edge scan extended well past
    // the curtain transit window (likely sensor photodiode/TIA recovery, not
    // shutter mechanics).
    float open_edge_width  = open90 - open10;
    float close_edge_width = close10 - close90;
    if (open_edge_width > 0.0f &&
        close_edge_width > MAX_CLOSE_EDGE_WIDTH_RATIO * open_edge_width) {
        return stats;  // edges_detected=true, valid stays false
    }

    stats.valid = true;
    return stats;
}

#include "shutter_measure.h"

#if IS_SHUTTER_TESTER

#include "shutter_capture.h"
#include "shutter_session.h"
#include "log_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#define TAG "ShutterMeas"

// ============================================================================
// Standard Shutter Speed Table
// ============================================================================

struct StandardSpeed {
    const char* label;       // e.g. "1/125s"
    float       duration_ms; // Nominal duration in ms
};

static const StandardSpeed STANDARD_SPEEDS[] = {
    { "4s",      4000.0f   },
    { "2s",      2000.0f   },
    { "1s",      1000.0f   },
    { "1/2s",    500.0f    },
    { "1/4s",    250.0f    },
    { "1/5s",    200.0f    },
    { "1/8s",    125.0f    },
    { "1/10s",   100.0f    },
    { "1/15s",   66.67f    },
    { "1/25s",   40.0f     },
    { "1/30s",   33.33f    },
    { "1/50s",   20.0f     },
    { "1/60s",   16.67f    },
    { "1/100s",  10.0f     },
    { "1/125s",  8.0f      },
    { "1/200s",  5.0f      },
    { "1/250s",  4.0f      },
    { "1/500s",  2.0f      },
    { "1/1000s", 1.0f      },
    { "1/2000s", 0.5f      },
};
static const int STANDARD_SPEEDS_COUNT = sizeof(STANDARD_SPEEDS) / sizeof(STANDARD_SPEEDS[0]);

// ============================================================================
// Module State
// ============================================================================

static ShutterMeasurement s_latest;
static ShutterMeasurement s_history[SHUTTER_HISTORY_SIZE];
static uint8_t  s_history_count = 0;
static uint8_t  s_history_head  = 0;   // Next write position (circular)
static uint32_t s_measure_count = 0;
static uint32_t s_last_capture_id = 0; // Dedup: capture_id of last processed capture
// FreeRTOS mutex protecting s_latest, s_history, s_measure_count, s_target_idx, s_speed_locked.
// Use a mutex (not portMUX spinlock) because critical sections may span large struct copies.
static SemaphoreHandle_t s_mutex = nullptr;

// Cached capture capabilities — populated once in shutter_measure_init().
static const char* s_preset_id_str_cached = nullptr;
static uint8_t     s_caps_sensor_count    = 0;

// Target lock state
static int  s_target_idx   = -1;    // -1 = not yet set (auto-detect)
static bool s_speed_locked = false; // When true, freeze target across captures

// Sensor layout offsets — set once via shutter_measure_set_geometry().
static float s_sensor_offset_x = 0.0f;
static float s_sensor_offset_y = 0.0f;
static float s_diagonal_mm     = 0.0f;

// Per-sensor position array — set via shutter_measure_set_geometry().
static ShutterSensorPosition s_positions[SHUTTER_SENSOR_MAX] = {};
static uint8_t s_position_count = 0;
static ShutterTopologyType s_topology = ShutterTopologyType::ThreeLine;

// ============================================================================
// Per-Sensor Duration Computation
// ============================================================================

// Linear regression on samples in [range_start..range_end] whose value falls
// within (lo_val, hi_val). Returns the sample-index (sub-sample, as float) at
// which the fitted line y = m*i + b crosses target_val. Returns NAN if fewer
// than 3 qualifying samples or the slope is too small to invert.
//
// Used by compute_sensor_duration() for slope-based edge extrapolation.
static float slope_fit_edge_crossing(const uint16_t* samples,
                                     uint32_t range_start, uint32_t range_end,
                                     uint16_t lo_val, uint16_t hi_val,
                                     uint16_t target_val) {
    if (!samples || range_end <= range_start) return NAN;
    double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0;
    int n = 0;
    for (uint32_t i = range_start; i <= range_end; i++) {
        uint16_t v = samples[i];
        if (v > lo_val && v < hi_val) {
            double x = (double)i;
            double y = (double)v;
            sum_x  += x;
            sum_y  += y;
            sum_xx += x * x;
            sum_xy += x * y;
            n++;
        }
    }
    if (n < 3) return NAN;
    double denom = (double)n * sum_xx - sum_x * sum_x;
    if (fabs(denom) < 1e-9) return NAN;
    double m = ((double)n * sum_xy - sum_x * sum_y) / denom;
    if (fabs(m) < 1e-6) return NAN;
    double b = (sum_y - m * sum_x) / (double)n;
    return (float)(((double)target_val - b) / m);
}

// Compute exposure duration for a single sensor by finding the first and last
// threshold crossing in the waveform.
static ShutterSensorResult compute_sensor_duration(const ShutterWaveformView* wf,
                                                    uint16_t threshold) {
    ShutterSensorResult result = {};
    if (!wf || wf->count == 0 || !wf->samples) return result;

    // Compute baseline: average and RMS noise of up to 200 samples before pulse.
    // Use samples before start_idx (or first 200 if no pulse yet found).
    // We compute baseline first with a preliminary pass.
    uint32_t baseline_count = (wf->count > 200) ? 200 : wf->count;
    uint32_t baseline_sum = 0;
    for (uint32_t i = 0; i < baseline_count; i++) {
        baseline_sum += wf->samples[i];
    }
    result.baseline_adc = (uint16_t)(baseline_sum / baseline_count);

    // Find first sample below threshold (pulse start).
    uint32_t start_idx = UINT32_MAX;
    for (uint32_t i = 0; i < wf->count; i++) {
        if (wf->samples[i] < threshold) {
            start_idx = i;
            break;
        }
    }
    if (start_idx == UINT32_MAX) return result; // No pulse found.

    // Recompute baseline and RMS from up to 200 samples immediately before the pulse.
    uint32_t rms_start = (start_idx > 200) ? (start_idx - 200) : 0;
    uint32_t rms_count = start_idx - rms_start;
    if (rms_count > 0) {
        uint32_t rms_sum = 0;
        for (uint32_t i = rms_start; i < start_idx; i++) {
            rms_sum += wf->samples[i];
        }
        uint16_t mean = (uint16_t)(rms_sum / rms_count);
        result.baseline_adc = mean;
        uint64_t sq_sum = 0;
        for (uint32_t i = rms_start; i < start_idx; i++) {
            int32_t diff = (int32_t)wf->samples[i] - (int32_t)mean;
            sq_sum += (uint64_t)(diff * diff);
        }
        result.idle_noise_rms = (uint16_t)sqrtf((float)sq_sum / (float)rms_count);
    }

    // First pass: find last sample below the fixed threshold to bound the pulse
    // region, then locate min ADC within that region.
    uint32_t coarse_end = start_idx;
    for (uint32_t i = wf->count; i > start_idx; i--) {
        if (wf->samples[i - 1] < threshold) {
            coarse_end = i - 1;
            break;
        }
    }

    // Find minimum ADC value during the pulse and its index.
    uint16_t min_val = 4095;
    uint32_t min_idx = start_idx;
    for (uint32_t i = start_idx; i <= coarse_end; i++) {
        if (wf->samples[i] < min_val) {
            min_val = wf->samples[i];
            min_idx = i;
        }
    }
    result.min_adc = min_val;

    // ----------------------------------------------------------------------
    // Slope-based edge extrapolation (primary method).
    //
    // The current 50% midpoint approach is brightness-invariant for clean
    // RC pulses but drifts when the bottom of the pulse is distorted by
    // amplifier slew limits, ADC rail clipping, or optical scatter
    // pedestals. These distortions affect min_val and therefore shift the
    // 50% threshold.
    //
    // Slope fit avoids that pitfall: fit a line through the LINEAR portion
    // (20%-80% of observed depth) of each edge, then compute where that
    // line crosses the 50% threshold. The fit ignores the distorted
    // bottom region and the noisy near-baseline region, giving a more
    // stable estimate of the edge timing.
    //
    // Requires at least 3 samples per edge in the linear window. Otherwise
    // falls back to the original 2-sample interpolation method below.
    // ----------------------------------------------------------------------
    int32_t depth = (int32_t)result.baseline_adc - (int32_t)min_val;
    if (depth >= SHUTTER_MIN_PULSE_DEPTH && min_idx > start_idx && min_idx < coarse_end) {
        uint16_t fit_lo = (uint16_t)(min_val + (depth * 1) / 5);          // 20% above min
        uint16_t fit_hi = (uint16_t)(min_val + (depth * 4) / 5);          // 80% above min
        uint16_t target = (uint16_t)(min_val + depth / 2);                // 50% point

        float t_fall = slope_fit_edge_crossing(wf->samples, start_idx, min_idx,
                                               fit_lo, fit_hi, target);
        float t_rise = slope_fit_edge_crossing(wf->samples, min_idx, coarse_end,
                                               fit_lo, fit_hi, target);

        if (!isnan(t_fall) && !isnan(t_rise) && t_rise > t_fall && wf->sample_rate_hz > 0.0f) {
            float duration_samples_f = t_rise - t_fall;
            result.duration_ms   = duration_samples_f / wf->sample_rate_hz * 1000.0f;
            result.start_idx     = (uint32_t)t_fall;
            result.end_idx       = (uint32_t)t_rise;
            result.total_samples = wf->count;
            result.valid         = true;
            return result;
        }
        // Fall through to legacy adaptive-threshold method.
    }

    // Adaptive threshold: 50% point between baseline and min ADC.
    // This makes timing independent of light intensity.
    uint16_t adaptive_thr = result.baseline_adc - (result.baseline_adc - min_val) / 2;

    // Second pass: re-scan with adaptive threshold for precise start/end.
    uint32_t end_idx = start_idx;
    start_idx = UINT32_MAX;
    for (uint32_t i = 0; i < wf->count; i++) {
        if (wf->samples[i] < adaptive_thr) {
            start_idx = i;
            break;
        }
    }
    if (start_idx == UINT32_MAX) return result; // Should not happen.
    for (uint32_t i = wf->count; i > start_idx; i--) {
        if (wf->samples[i - 1] < adaptive_thr) {
            end_idx = i - 1;
            break;
        }
    }

    if (end_idx <= start_idx || wf->sample_rate_hz <= 0.0f) return result;

    // Sub-sample interpolation at leading edge (falling through threshold).
    // start_idx is the first sample BELOW adaptive_thr.
    // Interpolate between samples[start_idx - 1] (above) and samples[start_idx] (below).
    float precise_start = (float)start_idx;
    if (start_idx > 0 && wf->samples[start_idx - 1] >= adaptive_thr) {
        float y_above = (float)wf->samples[start_idx - 1];
        float y_below = (float)wf->samples[start_idx];
        float thr_f   = (float)adaptive_thr;
        if (y_above != y_below) {
            precise_start = (float)(start_idx - 1) + (y_above - thr_f) / (y_above - y_below);
        }
    }

    // Sub-sample interpolation at trailing edge (rising through threshold).
    // end_idx is the last sample BELOW adaptive_thr.
    // Interpolate between samples[end_idx] (below) and samples[end_idx + 1] (above).
    float precise_end = (float)end_idx;
    if (end_idx + 1 < wf->count && wf->samples[end_idx + 1] >= adaptive_thr) {
        float y_below = (float)wf->samples[end_idx];
        float y_above = (float)wf->samples[end_idx + 1];
        float thr_f   = (float)adaptive_thr;
        if (y_above != y_below) {
            precise_end = (float)end_idx + (thr_f - y_below) / (y_above - y_below);
        }
    }

    float duration_samples_f = precise_end - precise_start;
    if (duration_samples_f <= 0.0f) return result;

    result.duration_ms = duration_samples_f / wf->sample_rate_hz * 1000.0f;
    result.start_idx = start_idx;
    result.end_idx = end_idx;
    result.total_samples = wf->count;
    result.valid = true;
    return result;
}

// ============================================================================
// Nearest Standard Speed Lookup
// ============================================================================

static int find_nearest_standard(float duration_ms) {
    int best = 0;
    float best_ratio = fabsf(logf(duration_ms / STANDARD_SPEEDS[0].duration_ms));

    for (int i = 1; i < STANDARD_SPEEDS_COUNT; i++) {
        float ratio = fabsf(logf(duration_ms / STANDARD_SPEEDS[i].duration_ms));
        if (ratio < best_ratio) {
            best_ratio = ratio;
            best = i;
        }
    }
    return best;
}

// ============================================================================
// Verdict evaluation — deviation only.
// SYNC: keep in sync with _rowVerdict() in portal_shutter_sessions.js.
// Capping, spread, and repeatability are still computed and shown to the
// technician but no longer drive the verdict; interpretation is left to them.
// ============================================================================

static ShutterVerdict evaluate_verdict(const ShutterMeasurement* m) {
    float abs_dev = fabsf(m->deviation_stops);
    if (abs_dev > SHUTTER_VERDICT_DEVIATION_FAIL)    return SHUTTER_VERDICT_FAIL;
    if (abs_dev > SHUTTER_VERDICT_DEVIATION_WARNING) return SHUTTER_VERDICT_WARNING;
    return SHUTTER_VERDICT_PASS;
}

// ============================================================================
// Measurement Processing
// ============================================================================

void shutter_measure_process_capture(const ShutterCaptureFrame* frame, ShutterMeasurement* m) {
    memset(m, 0, sizeof(ShutterMeasurement));
    m->capping_gradient_stops_per_mm = -1.0f;
    m->capping_gradient_x_stops_per_mm = -1.0f;
    m->capping_gradient_y_stops_per_mm = -1.0f;
    m->skew_differential_us_per_mm = -1.0f;
    m->curtain1_skew_left_us = 0.0f;
    m->curtain1_skew_right_us = 0.0f;
    m->curtain2_skew_left_us = 0.0f;
    m->curtain2_skew_right_us = 0.0f;
    m->detected_travel[0] = '\0';
    if (!frame || !frame->valid || frame->sensor_count == 0) return;

    m->sensor_count = frame->sensor_count;
    m->preset_id    = frame->preset_id;
    m->capture_id   = frame->capture_id;

    // Compute per-sensor durations using per-sensor thresholds from the frame.
    float sum_ms = 0.0f;
    float min_ms = 1e9f;
    float max_ms = 0.0f;
    int valid_count = 0;

    for (int i = 0; i < frame->sensor_count; i++) {
        m->sensors[i] = compute_sensor_duration(&frame->waveforms[i], frame->thresholds[i]);
        m->sensors[i].threshold = frame->thresholds[i];
        if (m->sensors[i].valid) {
            // Reject sensor if pulse depth is too shallow (noise trigger).
            uint16_t depth = m->sensors[i].baseline_adc - m->sensors[i].min_adc;
            if (depth < SHUTTER_MIN_PULSE_DEPTH) {
                m->sensors[i].valid = false;
                continue;
            }
            // Duration floor: reject sub-sample noise blips.
            if (m->sensors[i].duration_ms < SHUTTER_MIN_PULSE_DURATION_MS) {
                LOGD(TAG, "Sensor %d rejected: duration %.2f ms < minimum %.1f ms",
                     i, m->sensors[i].duration_ms, (double)SHUTTER_MIN_PULSE_DURATION_MS);
                m->sensors[i].valid = false;
                continue;
            }
            // Buffer coverage ceiling: reject ambient drift spanning most of the capture.
            if (m->sensors[i].total_samples > 0) {
                float coverage = (float)(m->sensors[i].end_idx - m->sensors[i].start_idx)
                               / (float)m->sensors[i].total_samples;
                if (coverage > SHUTTER_MAX_PULSE_COVERAGE) {
                    LOGD(TAG, "Sensor %d rejected: coverage %.1f%% > maximum %.0f%%",
                         i, coverage * 100.0f, (double)(SHUTTER_MAX_PULSE_COVERAGE * 100.0f));
                    m->sensors[i].valid = false;
                    continue;
                }
            }
            float d = m->sensors[i].duration_ms;
            sum_ms += d;
            if (d < min_ms) min_ms = d;
            if (d > max_ms) max_ms = d;
            valid_count++;
        }
    }

    m->valid_sensor_count = (uint8_t)valid_count;

    // Coherence check: reject if valid sensors disagree wildly.
    if (valid_count >= 2 && min_ms > 0.0f) {
        float ratio = max_ms / min_ms;
        if (ratio > SHUTTER_MAX_SENSOR_RATIO) {
            LOGW(TAG, "Measurement rejected: sensor duration ratio %.1f exceeds limit %.1f",
                 ratio, (double)SHUTTER_MAX_SENSOR_RATIO);
            for (int i = 0; i < frame->sensor_count; i++) {
                m->sensors[i].valid = false;
            }
            m->valid_sensor_count = 0;
            m->valid = false;
            return;
        }
    }

    if (valid_count == 0) {
        m->valid = false;
        return;
    }

    m->avg_duration_ms = sum_ms / (float)valid_count;
    m->timestamp_ms    = frame->timestamp_ms;
    m->valid           = true;

    // Spread: only meaningful with 2+ valid sensors.
    if (valid_count > 1 && m->avg_duration_ms > 0.0f) {
        m->spread_ms  = max_ms - min_ms;
        m->spread_pct = m->spread_ms / m->avg_duration_ms * 100.0f;
    }

    // Capping gradient: stops per mm along the sensor diagonal.
    // Only meaningful with 2+ valid sensors, configured geometry, and non-4-sensor topology.
    // For FourSensor presets, the 2D version below is used instead.
    if (valid_count > 1 && s_diagonal_mm > 0.0f && min_ms > 0.0f &&
        s_topology != ShutterTopologyType::FourSensor) {
        m->capping_gradient_stops_per_mm = fabsf(log2f(max_ms / min_ms)) / s_diagonal_mm;
    }

    // 2D capping gradient, curtain twist, and auto-detect for FourSensor topology.
    // Requires all 4 sensors valid. Positions: S1=TL, S2=TR, S3=BL, S4=BR.
    if (s_topology == ShutterTopologyType::FourSensor && valid_count == 4 &&
        s_position_count >= 4) {
        float e_left  = (m->sensors[0].duration_ms + m->sensors[2].duration_ms) / 2.0f;
        float e_right = (m->sensors[1].duration_ms + m->sensors[3].duration_ms) / 2.0f;
        float e_top   = (m->sensors[0].duration_ms + m->sensors[1].duration_ms) / 2.0f;
        float e_bot   = (m->sensors[2].duration_ms + m->sensors[3].duration_ms) / 2.0f;

        // Derive spans from the position array (not hardcoded).
        float col_span = fabsf(s_positions[1].x_mm - s_positions[0].x_mm);
        float row_span = fabsf(s_positions[0].y_mm - s_positions[2].y_mm);

        if (e_left > 0.0f && e_right > 0.0f && col_span > 0.0f)
            m->capping_gradient_x_stops_per_mm = fabsf(log2f(e_right / e_left)) / col_span;
        if (e_top > 0.0f && e_bot > 0.0f && row_span > 0.0f)
            m->capping_gradient_y_stops_per_mm = fabsf(log2f(e_bot / e_top)) / row_span;

        // 1D compat: gradient magnitude for verdict and legacy consumers.
        float gx = m->capping_gradient_x_stops_per_mm;
        float gy = m->capping_gradient_y_stops_per_mm;
        if (gx >= 0.0f && gy >= 0.0f)
            m->capping_gradient_stops_per_mm = sqrtf(gx * gx + gy * gy);

        // Auto-detect shutter travel direction from trigger order.
        // Must run before per-curtain skew so skew uses correct cross-axis pairs.
        float rate = frame->waveforms[0].sample_rate_hz;
        if (rate > 0.0f) {
            float t0 = (float)m->sensors[0].start_idx / rate;
            float t1 = (float)m->sensors[1].start_idx / rate;
            float t2 = (float)m->sensors[2].start_idx / rate;
            float t3 = (float)m->sensors[3].start_idx / rate;
            float dt_rows = fabsf((t2 + t3) / 2.0f - (t0 + t1) / 2.0f);
            float dt_cols = fabsf((t1 + t3) / 2.0f - (t0 + t2) / 2.0f);
            float sample_period = 1.0f / rate;
            if (dt_rows > 3.0f * dt_cols)
                strlcpy(m->detected_travel, "V", sizeof(m->detected_travel));
            else if (dt_cols > 3.0f * dt_rows)
                strlcpy(m->detected_travel, "H", sizeof(m->detected_travel));
            else if (dt_rows < 2.0f * sample_period && dt_cols < 2.0f * sample_period)
                strlcpy(m->detected_travel, "L", sizeof(m->detected_travel));
            // else: stays "" (ambiguous)
        }

        // Curtain skew from arrival/departure times.
        // The per-position skew measures cross-axis tilt: how much one side
        // of the curtain edge leads/lags the other side at a given travel
        // position.  The sensor pairs depend on detected travel direction:
        //   H travel: cross-axis = Y → left pair (TL,BL), right pair (TR,BR)
        //   V travel: cross-axis = X → bottom pair (BL,BR), top pair (TL,TR)
        // "left" = low travel-axis position, "right" = high travel-axis position.
        bool is_vertical = (m->detected_travel[0] == 'V');
        if (rate > 0.0f && col_span > 0.0f) {
            // Curtain 1 (opening) timing in seconds.
            float c1_t0 = (float)m->sensors[0].start_idx / rate;  // TL
            float c1_t1 = (float)m->sensors[1].start_idx / rate;  // TR
            float c1_t2 = (float)m->sensors[2].start_idx / rate;  // BL
            float c1_t3 = (float)m->sensors[3].start_idx / rate;  // BR

            if (is_vertical) {
                // V travel: cross-axis = X. Measure right-minus-left at bottom and top rows.
                // "left" = bottom row (low Y = low travel position).
                // "right" = top row (high Y = high travel position).
                m->curtain1_skew_left_us  = (c1_t3 - c1_t2) * 1e6f;  // BR - BL
                m->curtain1_skew_right_us = (c1_t1 - c1_t0) * 1e6f;  // TR - TL
            } else {
                // H travel (default): cross-axis = Y. Measure bottom-minus-top at left and right cols.
                m->curtain1_skew_left_us  = (c1_t2 - c1_t0) * 1e6f;  // BL - TL
                m->curtain1_skew_right_us = (c1_t3 - c1_t1) * 1e6f;  // BR - TR
            }

            // Curtain 2 (closing) timing in seconds.
            float c2_t0 = (float)m->sensors[0].end_idx / rate;  // TL
            float c2_t1 = (float)m->sensors[1].end_idx / rate;  // TR
            float c2_t2 = (float)m->sensors[2].end_idx / rate;  // BL
            float c2_t3 = (float)m->sensors[3].end_idx / rate;  // BR

            if (is_vertical) {
                m->curtain2_skew_left_us  = (c2_t3 - c2_t2) * 1e6f;  // BR - BL
                m->curtain2_skew_right_us = (c2_t1 - c2_t0) * 1e6f;  // TR - TL
            } else {
                m->curtain2_skew_left_us  = (c2_t2 - c2_t0) * 1e6f;  // BL - TL
                m->curtain2_skew_right_us = (c2_t3 - c2_t1) * 1e6f;  // BR - TR
            }

            // Differential skew (summary metric): row-average skew difference.
            float skew_top = (c1_t1 - c1_t0) / col_span * 1e6f;  // µs/mm
            float skew_bot = (c1_t3 - c1_t2) / col_span * 1e6f;
            m->skew_differential_us_per_mm = skew_bot - skew_top;
        }
    }

    // Nearest standard speed — locked or auto-detected.
    int idx;
    if (s_speed_locked && s_target_idx >= 0) {
        idx = s_target_idx;
    } else {
        idx = find_nearest_standard(m->avg_duration_ms);
        // Protect write: s_target_idx is shared with REST/button-action callers.
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_target_idx = idx;
        xSemaphoreGive(s_mutex);
    }
    strlcpy(m->nearest_speed, STANDARD_SPEEDS[idx].label, sizeof(m->nearest_speed));
    m->nearest_duration_ms = STANDARD_SPEEDS[idx].duration_ms;
    m->target_manual = (s_speed_locked || s_target_idx != idx);
    m->speed_locked  = s_speed_locked;

    // Deviation from nominal.
    if (m->nearest_duration_ms > 0.0f) {
        m->deviation_pct = (m->avg_duration_ms - m->nearest_duration_ms)
                           / m->nearest_duration_ms * 100.0f;
        m->deviation_stops = log2f(m->avg_duration_ms / m->nearest_duration_ms);
    }

    // Multi-metric verdict: deviation (primary), frame capping (primary), spread (secondary).
    m->verdict = evaluate_verdict(m);
}

// ============================================================================
// History Management
// ============================================================================

static void push_history(const ShutterMeasurement* m) {
    s_history[s_history_head] = *m;
    s_history_head = (s_history_head + 1) % SHUTTER_HISTORY_SIZE;
    if (s_history_count < SHUTTER_HISTORY_SIZE) {
        s_history_count++;
    }
}

// ============================================================================
// Public API
// ============================================================================

void shutter_measure_init() {
    memset(&s_latest, 0, sizeof(s_latest));
    memset(s_history, 0, sizeof(s_history));
    s_history_count   = 0;
    s_history_head    = 0;
    s_measure_count   = 0;
    s_last_capture_id = 0;
    s_target_idx      = -1;
    s_speed_locked    = false;

    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();

    // Cache capture capabilities once at init to avoid repeated calls per measurement.
    ShutterCaptureCaps caps = {};
    shutter_capture_get_caps(&caps);
    s_preset_id_str_cached = (caps.preset_id_str && caps.preset_id_str[0])
                                 ? caps.preset_id_str : "unknown";
    s_caps_sensor_count = caps.sensor_count > 0 ? caps.sensor_count : 1;

    LOGI(TAG, "Measurement engine initialized");

    // Emit CSV header at boot for raw serial / monitor.sh users.
    // NOTE: monitor_meas.sh generates its own header from CSV_HEADER_TOP / CSV_SENSOR_COLS.
    // If column names or order change here, update those variables in monitor_meas.sh to match.
    Serial.print("[MEAS] #,preset_id,capture_id,timestamp_ms,matched_speed,matched_ms,"
                 "target_manual,speed_locked,avg_ms,dev_pct,dev_stops,spread_ms,"
                 "spread_pct,verdict,sensor_count,valid_sensor_count,"
                 "capping_x,capping_y,skew_diff_us_mm,detected_travel,"
                 "c1_skew_left_us,c1_skew_right_us,c2_skew_left_us,c2_skew_right_us");
    for (int i = 0; i < s_caps_sensor_count; i++) {
        Serial.printf(",S%d_ms,S%d_min,S%d_base,S%d_rms,S%d_depth,S%d_snr_db,"
                      "S%d_threshold,S%d_start,S%d_end,S%d_total",
                      i+1,i+1,i+1,i+1,i+1,i+1,i+1,i+1,i+1,i+1);
    }
    Serial.println();
}

void shutter_measure_process() {
    shutter_capture_poll();
    ShutterCaptureFrame frame;
    if (!shutter_capture_get_latest(&frame)) return;
    if (!frame.valid) return;

    // Dedup: skip if we already processed this capture.
    if (frame.capture_id != 0 && frame.capture_id == s_last_capture_id) return;
    s_last_capture_id = frame.capture_id;

    ShutterMeasurement m;
    shutter_measure_process_capture(&frame, &m);
    if (!m.valid) {
        LOGW(TAG, "Capture produced no valid measurement");
        return;
    }

    LOGI(TAG, "Measurement #%lu: %s (%.1f ms, dev %+.1f%%, spread %.1f%%, %s)",
         (unsigned long)(s_measure_count + 1),
         m.nearest_speed, m.avg_duration_ms, m.deviation_pct, m.spread_pct,
         m.verdict == SHUTTER_VERDICT_PASS ? "PASS" :
         m.verdict == SHUTTER_VERDICT_WARNING ? "WARN" : "FAIL");

    // Snapshot the row number under the lock before emitting the CSV line so
    // the printed value matches the count stored after the increment (ARCH-02).
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t row_num = s_measure_count + 1;
    xSemaphoreGive(s_mutex);

    // CSV diagnostic line.
    Serial.printf("[MEAS] %lu,%s,%lu,%lu,%s,%.3f,%d,%d,%.2f,%+.2f,%.4f,%.3f,%.2f,%d,%d,%d",
                  (unsigned long)row_num,
                  s_preset_id_str_cached,
                  (unsigned long)m.capture_id,
                  (unsigned long)m.timestamp_ms,
                  m.nearest_speed,
                  m.nearest_duration_ms,
                  m.target_manual ? 1 : 0,
                  m.speed_locked ? 1 : 0,
                  m.avg_duration_ms,
                  m.deviation_pct,
                  m.deviation_stops,
                  m.spread_ms,
                  m.spread_pct,
                  (int)m.verdict,
                  m.sensor_count,
                  m.valid_sensor_count);
    // New 4-corner columns (appended after per-measurement block, before per-sensor blocks).
    Serial.printf(",%.6f,%.6f,%.4f,%s,%.2f,%.2f,%.2f,%.2f",
                  m.capping_gradient_x_stops_per_mm,
                  m.capping_gradient_y_stops_per_mm,
                  m.skew_differential_us_per_mm,
                  m.detected_travel,
                  m.curtain1_skew_left_us, m.curtain1_skew_right_us,
                  m.curtain2_skew_left_us, m.curtain2_skew_right_us);
    for (int i = 0; i < m.sensor_count; i++) {
        const ShutterSensorResult& s = m.sensors[i];
        if (s.valid) {
            uint16_t depth = s.baseline_adc - s.min_adc;
            Serial.printf(",%.2f,%d,%d,%d,%d,",
                          s.duration_ms, s.min_adc, s.baseline_adc,
                          s.idle_noise_rms, (int)depth);
            if (depth > 0 && s.idle_noise_rms > 0) {
                Serial.printf("%.1f", 20.0f * log10f((float)depth / (float)s.idle_noise_rms));
            }
            Serial.printf(",%d,%lu,%lu,%lu",
                          s.threshold,
                          (unsigned long)s.start_idx, (unsigned long)s.end_idx,
                          (unsigned long)s.total_samples);
        } else {
            // SHUTTER_SENSOR_CSV_COL_COUNT empty cells for invalid/missing sensor.
            Serial.print(",,,,,,,,,,");
        }
    }
    Serial.println();

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_latest = m;
    push_history(&m);
    s_measure_count++;
    xSemaphoreGive(s_mutex);
    shutter_session_on_measurement(&m);
}

bool shutter_measure_get_latest(ShutterMeasurement* out) {
    if (!out) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool valid = s_latest.valid;
    if (valid) *out = s_latest;
    xSemaphoreGive(s_mutex);
    return valid;
}

uint8_t shutter_measure_get_history(ShutterMeasurement* out, uint8_t max_count) {
    if (!out || max_count == 0) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint8_t count = s_history_count;
    if (count > max_count) count = max_count;

    // Copy oldest-first from the circular buffer.
    uint8_t start;
    if (s_history_count < SHUTTER_HISTORY_SIZE) {
        start = 0;
    } else {
        start = s_history_head; // head points to oldest in a full buffer
    }
    for (uint8_t i = 0; i < count; i++) {
        uint8_t idx = (start + i) % SHUTTER_HISTORY_SIZE;
        out[i] = s_history[idx];
    }
    xSemaphoreGive(s_mutex);
    return count;
}

uint32_t shutter_measure_get_count() {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t c = s_measure_count;
    xSemaphoreGive(s_mutex);
    return c;
}

// ============================================================================
// Target Speed Control
// ============================================================================

// Recompute all target-dependent fields in s_latest using the current
// s_target_idx / s_speed_locked state.  Bumps s_measure_count so that
// change-detection tokens (bindings, waveform widget) pick up the update.
// Must be called WITHOUT s_mux held; acquires it internally.
static void recompute_target_fields() {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!s_latest.valid) {
        xSemaphoreGive(s_mutex);
        return;
    }
    int idx = (s_target_idx >= 0) ? s_target_idx
                                   : find_nearest_standard(s_latest.avg_duration_ms);
    strlcpy(s_latest.nearest_speed, STANDARD_SPEEDS[idx].label, sizeof(s_latest.nearest_speed));
    s_latest.nearest_duration_ms = STANDARD_SPEEDS[idx].duration_ms;
    s_latest.target_manual        = s_speed_locked;
    s_latest.speed_locked         = s_speed_locked;
    if (s_latest.nearest_duration_ms > 0.0f) {
        s_latest.deviation_pct   = (s_latest.avg_duration_ms - s_latest.nearest_duration_ms)
                                    / s_latest.nearest_duration_ms * 100.0f;
        s_latest.deviation_stops = log2f(s_latest.avg_duration_ms / s_latest.nearest_duration_ms);
    }
    s_latest.verdict = evaluate_verdict(&s_latest);
    s_measure_count++;   // bump so bindings + waveform re-render
    xSemaphoreGive(s_mutex);
    shutter_session_on_recompute();
}

bool shutter_measure_set_target(const char* label, bool recompute) {
    // Search for matching entry (label with or without trailing 's')
    char with_s[20];
    snprintf(with_s, sizeof(with_s), "%ss", label);
    for (int i = 0; i < STANDARD_SPEEDS_COUNT; i++) {
        if (strcmp(STANDARD_SPEEDS[i].label, with_s) == 0 ||
            strcmp(STANDARD_SPEEDS[i].label, label) == 0) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_target_idx = i;
            xSemaphoreGive(s_mutex);
            LOGI(TAG, "Target set: %s", STANDARD_SPEEDS[i].label);
            if (recompute) recompute_target_fields();
            return true;
        }
    }
    LOGW(TAG, "set_target: unknown speed '%s'", label);
    return false;
}

void shutter_measure_adjust_target(bool faster) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int idx = s_target_idx;
    // If no target is set yet, seed from the latest auto-detected speed so the
    // user can nudge relative to what was just measured.
    if (idx < 0 && s_latest.valid) {
        idx = find_nearest_standard(s_latest.avg_duration_ms);
    }
    xSemaphoreGive(s_mutex);
    if (idx < 0) {
        LOGW(TAG, "adjust_target: no current target and no measurement yet");
        return;
    }
    int new_idx = faster ? (idx + 1) : (idx - 1);
    if (new_idx < 0) new_idx = 0;
    if (new_idx >= STANDARD_SPEEDS_COUNT) new_idx = STANDARD_SPEEDS_COUNT - 1;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_target_idx = new_idx;
    xSemaphoreGive(s_mutex);
    LOGI(TAG, "Target adjust %s: %s", faster ? "faster" : "slower", STANDARD_SPEEDS[new_idx].label);
    recompute_target_fields();
}

bool shutter_measure_toggle_lock() {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!s_speed_locked && s_target_idx < 0) {
        xSemaphoreGive(s_mutex);
        LOGW(TAG, "toggle_lock: no target to lock");
        return false;
    }
    s_speed_locked = !s_speed_locked;
    bool locked = s_speed_locked;
    xSemaphoreGive(s_mutex);
    LOGI(TAG, "Speed lock: %s (target=%s)", locked ? "ON" : "OFF",
         s_target_idx >= 0 ? STANDARD_SPEEDS[s_target_idx].label : "none");
    recompute_target_fields();
    return true;
}

bool shutter_measure_set_lock(bool locked) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (locked && s_target_idx < 0) {
        xSemaphoreGive(s_mutex);
        LOGW(TAG, "set_lock: no target to lock");
        return false;
    }
    s_speed_locked = locked;
    int idx_snap = s_target_idx;
    xSemaphoreGive(s_mutex);
    LOGI(TAG, "Speed lock: %s (target=%s)", locked ? "ON" : "OFF",
         idx_snap >= 0 ? STANDARD_SPEEDS[idx_snap].label : "none");
    recompute_target_fields();
    return true;
}

void shutter_measure_get_target(char* out_label, size_t len, bool* out_locked) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int idx = s_target_idx;
    bool locked = s_speed_locked;
    xSemaphoreGive(s_mutex);
    if (out_locked) *out_locked = locked;
    if (out_label) {
        if (idx >= 0) strlcpy(out_label, STANDARD_SPEEDS[idx].label, len);
        else out_label[0] = '\0';
    }
}

bool shutter_measure_is_valid_speed(const char* label) {
    if (!label || !label[0]) return false;
    char with_s[20];
    snprintf(with_s, sizeof(with_s), "%ss", label);
    for (int i = 0; i < STANDARD_SPEEDS_COUNT; i++) {
        if (strcmp(STANDARD_SPEEDS[i].label, with_s) == 0 ||
            strcmp(STANDARD_SPEEDS[i].label, label) == 0) {
            return true;
        }
    }
    return false;
}

void shutter_measure_set_geometry(const ShutterSensorPosition* positions, uint8_t count) {
    memset(s_positions, 0, sizeof(s_positions));
    s_position_count = 0;
    s_diagonal_mm = 0.0f;
    s_sensor_offset_x = 0.0f;
    s_sensor_offset_y = 0.0f;

    if (!positions || count == 0) return;
    if (count > SHUTTER_SENSOR_MAX) count = SHUTTER_SENSOR_MAX;

    memcpy(s_positions, positions, count * sizeof(ShutterSensorPosition));
    s_position_count = count;

    // Compute legacy diagonal_mm as Euclidean distance from first to last sensor.
    if (count >= 2) {
        float dx = positions[count - 1].x_mm - positions[0].x_mm;
        float dy = positions[count - 1].y_mm - positions[0].y_mm;
        s_diagonal_mm = sqrtf(dx * dx + dy * dy);
    }

    // For 3-sensor symmetric layouts, populate legacy offset values.
    if (count == 3) {
        s_sensor_offset_x = fabsf(positions[2].x_mm);
        s_sensor_offset_y = fabsf(positions[2].y_mm);
    }

    // Cache topology from capture caps.
    ShutterCaptureCaps caps = {};
    shutter_capture_get_caps(&caps);
    s_topology = caps.topology;
}

uint8_t shutter_measure_get_geometry(ShutterSensorPosition* out, uint8_t max_count) {
    if (out && max_count >= s_position_count) {
        memcpy(out, s_positions, s_position_count * sizeof(ShutterSensorPosition));
    }
    return s_position_count;
}

void shutter_measure_get_sensor_offsets(float* out_x, float* out_y, float* out_diagonal) {
    if (out_x) *out_x = s_sensor_offset_x;
    if (out_y) *out_y = s_sensor_offset_y;
    if (out_diagonal) *out_diagonal = s_diagonal_mm;
}

#endif // IS_SHUTTER_TESTER

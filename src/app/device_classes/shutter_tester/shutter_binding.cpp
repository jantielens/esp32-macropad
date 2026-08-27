#include "shutter_binding.h"
#include "board_config.h"

#if HAS_DISPLAY && IS_SHUTTER_TESTER

#include "binding_template.h"
#include "shutter_measure.h"
#include "shutter_capture.h"
#include "shutter_session.h"
#include "shutter_align_binding.h"
#include "log_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define TAG "ShutterBind"

// ============================================================================
// Parse params: "key" or "key;format"
// ============================================================================

static void parse_params(const char* params,
                         char* key, size_t key_len,
                         char* fmt, size_t fmt_len) {
    key[0] = '\0';
    fmt[0] = '\0';
    if (!params || !params[0]) return;

    // Bracket-depth-aware scan: only split on ';' at depth 0 so that
    // inner bindings (e.g. [shutter:key;fmt]) are not confused with
    // a ';' nested inside an expression or pad binding parameter.
    const char* sep = nullptr;
    int depth = 0;
    for (const char* p = params; *p; p++) {
        if (*p == '[') { depth++; }
        else if (*p == ']') { if (depth > 0) depth--; }
        else if (*p == ';' && depth == 0) { sep = p; break; }
    }

    if (!sep) {
        strlcpy(key, params, key_len);
        return;
    }
    size_t klen = (size_t)(sep - params);
    if (klen >= key_len) klen = key_len - 1;
    memcpy(key, params, klen);
    key[klen] = '\0';
    strlcpy(fmt, sep + 1, fmt_len);
}

// ============================================================================
// Verdict string helper
// ============================================================================

static const char* verdict_str(ShutterVerdict v) {
    switch (v) {
        case SHUTTER_VERDICT_PASS:    return "pass";
        case SHUTTER_VERDICT_WARNING: return "warning";
        case SHUTTER_VERDICT_FAIL:    return "fail";
        default:                      return "---";
    }
}

static const char* preset_id_to_str(ShutterPresetId id) {
    switch (id) {
        case ShutterPresetId::DirectSingle:   return "direct_single";
        case ShutterPresetId::Direct3Line:    return "direct_3_line";
        case ShutterPresetId::Offload3Line:   return "offload_3_line";
        case ShutterPresetId::Offload9Matrix: return "offload_9_matrix";
        case ShutterPresetId::Direct4Corner:  return "direct_4_corner";
        case ShutterPresetId::Direct4LShapeH: return "direct_4_lshape_h";
        case ShutterPresetId::Direct4LShapeV: return "direct_4_lshape_v";
        default:                              return "unknown";
    }
}

// ============================================================================
// History JSON builder
// ============================================================================

// Static cache: only rebuild the JSON when the measurement count changes.
static uint32_t s_history_json_count = UINT32_MAX;
static char s_history_json_cache[512];

static bool build_history_json(char* out, size_t out_len) {
    uint32_t current_count = shutter_measure_get_count();
    if (current_count == s_history_json_count && s_history_json_cache[0]) {
        strlcpy(out, s_history_json_cache, out_len);
        return true;
    }

    // Allocate history array in PSRAM to avoid consuming ~1.28 KB of stack
    // (SHUTTER_HISTORY_SIZE × sizeof(ShutterMeasurement)).
    ShutterMeasurement* history = (ShutterMeasurement*)heap_caps_malloc(
        SHUTTER_HISTORY_SIZE * sizeof(ShutterMeasurement), MALLOC_CAP_SPIRAM);
    if (!history) {
        strlcpy(out, "[]", out_len);
        return false;
    }
    uint8_t count = shutter_measure_get_history(history, SHUTTER_HISTORY_SIZE);

    // Build JSON manually with snprintf to avoid Arduino String / ArduinoJson
    // zero-copy lifetime issues and to enable host-native testing.
    // Each entry: {"speed":"...","ms":"X.X","dev":"X.X%","verdict":"...",
    //              "preset_id":"...","sensor_count":N[,"spread":"X.X%"]}
    // Spread is included only for multi-sensor captures (sensor_count >= 2).
    char* p = s_history_json_cache;
    size_t remaining = sizeof(s_history_json_cache);

    auto emit = [&](const char* s) {
        if (remaining <= 1) return;
        size_t n = strlen(s);
        if (n >= remaining) n = remaining - 1;
        memcpy(p, s, n);
        p += n;
        remaining -= n;
        *p = '\0';
    };

    emit("[");
    for (uint8_t i = 0; i < count; i++) {
        const ShutterMeasurement& m = history[i];
        char entry[256];
        if (m.sensor_count >= 2) {
            snprintf(entry, sizeof(entry),
                "%s{\"speed\":\"%s\",\"ms\":\"%.1f\",\"dev\":\"%.1f%%%%\","
                "\"spread\":\"%.1f%%%%\",\"verdict\":\"%s\","
                "\"preset_id\":\"%s\",\"sensor_count\":%d}",
                i > 0 ? "," : "",
                m.nearest_speed, m.avg_duration_ms, m.deviation_pct,
                m.spread_pct, verdict_str(m.verdict),
                preset_id_to_str(m.preset_id), m.sensor_count);
        } else {
            snprintf(entry, sizeof(entry),
                "%s{\"speed\":\"%s\",\"ms\":\"%.1f\",\"dev\":\"%.1f%%%%\","
                "\"verdict\":\"%s\","
                "\"preset_id\":\"%s\",\"sensor_count\":%d}",
                i > 0 ? "," : "",
                m.nearest_speed, m.avg_duration_ms, m.deviation_pct,
                verdict_str(m.verdict),
                preset_id_to_str(m.preset_id), m.sensor_count);
        }
        emit(entry);
    }
    emit("]");

    heap_caps_free(history);
    s_history_json_count = current_count;
    strlcpy(out, s_history_json_cache, out_len);
    return remaining > 0;
}

// ============================================================================
// Session sub-domain resolver (handles keys after "session." prefix)
// ============================================================================

static bool lookup_session(const char* key, char* out, size_t out_len) {
    if (strcmp(key, "active") == 0) {
        strlcpy(out, shutter_session_is_active() ? "true" : "false", out_len);
        return true;
    }
    if (strcmp(key, "count") == 0) {
        snprintf(out, out_len, "%lu", (unsigned long)shutter_session_get_count());
        return true;
    }
    if (strcmp(key, "id") == 0) {
        uint32_t id = shutter_session_get_id();
        if (id == 0) strlcpy(out, "", out_len);
        else snprintf(out, out_len, "%lu", (unsigned long)id);
        return true;
    }
    if (strcmp(key, "type") == 0) {
        shutter_session_get_type(out, out_len);
        return true;
    }
    // Guided sub-domain: session.guide.*
    if (strncmp(key, "guide.", 6) == 0) {
        const char* gkey = key + 6;
        if (strcmp(gkey, "target") == 0) {
            shutter_session_guide_get_target(out, out_len);
            return true;
        }
        if (strcmp(gkey, "step") == 0) {
            shutter_session_guide_get_step(out, out_len);
            return true;
        }
        if (strcmp(gkey, "steps") == 0) {
            shutter_session_guide_get_steps(out, out_len);
            return true;
        }
        if (strcmp(gkey, "shot") == 0) {
            shutter_session_guide_get_shot(out, out_len);
            return true;
        }
        if (strcmp(gkey, "shots") == 0) {
            shutter_session_guide_get_shots(out, out_len);
            return true;
        }
        if (strcmp(gkey, "taking") == 0) {
            shutter_session_guide_get_taking(out, out_len);
            return true;
        }
        if (strcmp(gkey, "total") == 0) {
            shutter_session_guide_get_total(out, out_len);
            return true;
        }
        if (strcmp(gkey, "name") == 0) {
            shutter_session_guide_get_name(out, out_len);
            return true;
        }
        if (strcmp(gkey, "id") == 0) {
            shutter_session_guide_get_id(out, out_len);
            return true;
        }
        return false;
    }
    return false;
}

// ============================================================================
// Calibration sub-domain resolver (handles keys after "calib." prefix)
// ============================================================================

static bool lookup_calibration(const char* key, char* out, size_t out_len) {
    if (strcmp(key, "active") == 0) {
        snprintf(out, out_len, "%d", shutter_capture_is_calibrating() ? 1 : 0);
        return true;
    }
    return false;
}

// ============================================================================
// Measurement key resolver (flat keys, no prefix)
// ============================================================================

// Return true and clear the output buffer when capping data is unavailable or negligible.
static bool capping_not_available(float gradient, char* out) {
    if (gradient < 0.0f || gradient < 0.001f) { out[0] = '\0'; return true; }
    return false;
}

static bool lookup_measurement(const char* key, char* out, size_t out_len) {
    // Caps are static after init — fetch once for the whole call.
    ShutterCaptureCaps caps;
    shutter_capture_get_caps(&caps);

    // Keys that don't need a measurement.
    if (strcmp(key, "available") == 0) {
        strlcpy(out, shutter_capture_is_available() ? "true" : "false", out_len);
        return true;
    }
    if (strcmp(key, "sensor_count") == 0) {
        snprintf(out, out_len, "%d", caps.sensor_count);
        return true;
    }
    if (strcmp(key, "preset_id") == 0) {
        strlcpy(out, (caps.preset_id_str && caps.preset_id_str[0]) ? caps.preset_id_str : "---", out_len);
        return true;
    }
    if (strcmp(key, "preset_name") == 0) {
        strlcpy(out, (caps.preset_name && caps.preset_name[0]) ? caps.preset_name : "---", out_len);
        return true;
    }
    if (strcmp(key, "count") == 0) {
        snprintf(out, out_len, "%lu", (unsigned long)shutter_measure_get_count());
        return true;
    }
    if (strcmp(key, "capture_id") == 0) {
        // Returns the measurement count as a change-detection token.
        // Waveform widget compares this to know when to re-read capture data.
        snprintf(out, out_len, "%lu", (unsigned long)shutter_measure_get_count());
        return true;
    }
    if (strcmp(key, "target_speed") == 0) {
        // Full label including 's', e.g. "1/1000s". Empty if no target yet.
        char label[20] = {};
        bool locked;
        shutter_measure_get_target(label, sizeof(label), &locked);
        strlcpy(out, label[0] ? label : "---", out_len);
        return true;
    }
    if (strcmp(key, "speed_locked") == 0) {
        bool locked;
        shutter_measure_get_target(nullptr, 0, &locked);
        strlcpy(out, locked ? "true" : "false", out_len);
        return true;
    }

    // All other keys require a valid latest measurement. Force idle
    // placeholder when the ADC engine is not currently held by any consumer
    // \u2014 prevents stale post-session values from masking the fact that
    // the engine is parked (which is the normal state on shutter-tester
    // boards when no pad screen / session / alignment is active).
    ShutterMeasurement m;
    if (!shutter_capture_is_running() || !shutter_measure_get_latest(&m) || !m.valid) {
        strlcpy(out, "---", out_len);
        return true;  // Resolved, just no live data.
    }

    if (strcmp(key, "speed") == 0) {
        // Copy label without trailing 's' (e.g. "1/1000s" → "1/1000")
        strlcpy(out, m.nearest_speed, out_len);
        size_t len = strlen(out);
        if (len > 0 && out[len - 1] == 's') out[len - 1] = '\0';
        return true;
    }
    if (strcmp(key, "speed_seconds") == 0) {
        // Target speed as a decimal in seconds (e.g. 0.001 for 1/1000s)
        snprintf(out, out_len, "%g", (double)m.nearest_duration_ms / 1000.0);
        return true;
    }
    if (strcmp(key, "target_ms") == 0) {
        snprintf(out, out_len, "%.1f", m.nearest_duration_ms);
        return true;
    }
    if (strcmp(key, "duration_ms") == 0) {
        snprintf(out, out_len, "%.1f", m.avg_duration_ms);
        return true;
    }
    if (strcmp(key, "deviation") == 0) {
        snprintf(out, out_len, "%.1f", m.deviation_pct);
        return true;
    }
    if (strcmp(key, "deviation_abs") == 0) {
        snprintf(out, out_len, "%.1f", fabsf(m.deviation_pct));
        return true;
    }
    if (strcmp(key, "deviation_stops") == 0) {
        snprintf(out, out_len, "%.2f", m.deviation_stops);
        return true;
    }
    if (strcmp(key, "verdict") == 0) {
        strlcpy(out, verdict_str(m.verdict), out_len);
        return true;
    }
    if (strcmp(key, "spread") == 0) {
        if (m.sensor_count < 2) { strlcpy(out, "---", out_len); return true; }
        snprintf(out, out_len, "%.1f", m.spread_pct);
        return true;
    }
    if (strcmp(key, "spread_ms") == 0) {
        if (m.sensor_count < 2) { strlcpy(out, "---", out_len); return true; }
        snprintf(out, out_len, "%.2f", m.spread_ms);
        return true;
    }
    if (strcmp(key, "capping_gradient") == 0) {
        if (capping_not_available(m.capping_gradient_stops_per_mm, out)) return true;
        snprintf(out, out_len, "%.3f", m.capping_gradient_stops_per_mm);
        return true;
    }
    if (strcmp(key, "capping_frame_stops") == 0) {
        if (capping_not_available(m.capping_gradient_stops_per_mm, out)) return true;
        snprintf(out, out_len, "%.2f", m.capping_gradient_stops_per_mm * SHUTTER_FILM_DIAGONAL_MM);
        return true;
    }
    if (strcmp(key, "valid_sensor_count") == 0) {
        snprintf(out, out_len, "%d", m.valid_sensor_count);
        return true;
    }

    // Per-sensor durations (count-driven; returns '---' for sensors beyond active count).
    {
        for (int i = 0; i < SHUTTER_SENSOR_MAX; i++) {
            char sensor_key[16];
            snprintf(sensor_key, sizeof(sensor_key), "sensor_%d_ms", i + 1);
            if (strcmp(key, sensor_key) == 0) {
                if (i >= caps.sensor_count) {
                    strlcpy(out, "---", out_len);
                } else if (m.sensors[i].valid) {
                    snprintf(out, out_len, "%.1f", m.sensors[i].duration_ms);
                } else {
                    strlcpy(out, "---", out_len);
                }
                return true;
            }
        }
    }

    // Per-sensor health indicators (count-driven).
    {
        for (int i = 0; i < SHUTTER_SENSOR_MAX; i++) {
            char sensor_key[32];

            // Valid flag
            snprintf(sensor_key, sizeof(sensor_key), "sensor_%d_valid", i + 1);
            if (strcmp(key, sensor_key) == 0) {
                if (i >= caps.sensor_count) strlcpy(out, "---", out_len);
                else strlcpy(out, m.sensors[i].valid ? "true" : "false", out_len);
                return true;
            }

            // Pulse depth (signal strength)
            snprintf(sensor_key, sizeof(sensor_key), "sensor_%d_depth", i + 1);
            if (strcmp(key, sensor_key) == 0) {
                if (i >= caps.sensor_count || !m.sensors[i].valid) strlcpy(out, "---", out_len);
                else { int d = (int)m.sensors[i].baseline_adc - (int)m.sensors[i].min_adc; snprintf(out, out_len, "%d", d); }
                return true;
            }

            // Signal-to-noise ratio
            snprintf(sensor_key, sizeof(sensor_key), "sensor_%d_snr", i + 1);
            if (strcmp(key, sensor_key) == 0) {
                if (i >= caps.sensor_count || !m.sensors[i].valid || m.sensors[i].idle_noise_rms == 0) strlcpy(out, "---", out_len);
                else { int d = (int)m.sensors[i].baseline_adc - (int)m.sensors[i].min_adc; snprintf(out, out_len, "%d", d / (int)m.sensors[i].idle_noise_rms); }
                return true;
            }

            // Saturation check
            snprintf(sensor_key, sizeof(sensor_key), "sensor_%d_saturated", i + 1);
            if (strcmp(key, sensor_key) == 0) {
                if (i >= caps.sensor_count || !m.sensors[i].valid) strlcpy(out, "---", out_len);
                else strlcpy(out, m.sensors[i].min_adc < 100 ? "true" : "false", out_len);
                return true;
            }

            // Raw minimum ADC value during pulse
            snprintf(sensor_key, sizeof(sensor_key), "sensor_%d_min_adc", i + 1);
            if (strcmp(key, sensor_key) == 0) {
                if (i >= caps.sensor_count || !m.sensors[i].valid) strlcpy(out, "---", out_len);
                else snprintf(out, out_len, "%u", (unsigned)m.sensors[i].min_adc);
                return true;
            }

            // Adaptive threshold used during this capture
            snprintf(sensor_key, sizeof(sensor_key), "sensor_%d_threshold", i + 1);
            if (strcmp(key, sensor_key) == 0) {
                if (i >= caps.sensor_count || !m.sensors[i].valid) strlcpy(out, "---", out_len);
                else snprintf(out, out_len, "%u", (unsigned)m.sensors[i].threshold);
                return true;
            }
        }
    }

    // Worst-case noise across all active sensors.
    if (strcmp(key, "worst_noise_rms") == 0) {
        uint16_t worst = 0;
        bool any_valid = false;
        for (int i = 0; i < caps.sensor_count; i++) {
            if (m.sensors[i].valid) {
                any_valid = true;
                if (m.sensors[i].idle_noise_rms > worst)
                    worst = m.sensors[i].idle_noise_rms;
            }
        }
        if (any_valid) snprintf(out, out_len, "%u", (unsigned)worst);
        else strlcpy(out, "---", out_len);
        return true;
    }

    return false; // Unknown key.
}

// ============================================================================
// Prefix router: dispatches to sub-domain resolvers
// ============================================================================

static bool lookup_value(const char* key, char* out, size_t out_len) {
    if (strncmp(key, "align.", 6) == 0)
        return shutter_align_binding_resolve(key + 6, out, out_len);
    if (strncmp(key, "calib.", 6) == 0)
        return lookup_calibration(key + 6, out, out_len);
    if (strncmp(key, "session.", 8) == 0)
        return lookup_session(key + 8, out, out_len);
    return lookup_measurement(key, out, out_len);
}

// ============================================================================
// Scheme resolver
// ============================================================================

static BindingResolverStatus shutter_binding_resolve(const char* params, char* out, size_t out_len) {
    char key[32];
    char fmt[32];
    parse_params(params, key, sizeof(key), fmt, sizeof(fmt));

    if (!key[0]) {
        strlcpy(out, "ERR:no key", out_len);
        return BINDING_RESOLVER_UNAVAILABLE;
    }

    // history_json is routed directly to the output buffer to avoid truncation
    // by the 192-byte BINDING_TEMPLATE_MAX_LEN intermediate copy below.
    if (strcmp(key, "history_json") == 0) {
        return build_history_json(out, out_len) ? BINDING_RESOLVER_RESOLVED
                            : BINDING_RESOLVER_UNAVAILABLE;
    }

    char raw[512]; // Larger intermediate buffer for numeric/string values.
    if (!lookup_value(key, raw, sizeof(raw))) {
        strlcpy(out, "ERR:bad key", out_len);
        return BINDING_RESOLVER_UNAVAILABLE;
    }

    if (fmt[0]) {
        char* end = nullptr;
        double dval = strtod(raw, &end);
        if (end && *end == '\0') {
            snprintf(out, out_len, fmt, dval);
        } else {
            // Value is non-numeric (e.g. "---"): emit as-is, ignore format.
            strlcpy(out, raw, out_len);
        }
    } else {
        strlcpy(out, raw, out_len);
    }
    return BINDING_RESOLVER_RESOLVED;
}

// ============================================================================
// No-op topic collector — shutter data is local
// ============================================================================

static void shutter_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Init
// ============================================================================

void shutter_binding_init() {
    if (!binding_template_register("shutter", shutter_binding_resolve, shutter_binding_collect,
                                   {1, 2, 1, 1, BINDING_VALIDATION_STANDARD, true, nullptr, nullptr})) {
        LOGE(TAG, "Failed to register shutter binding scheme");
    }
}

#else // !HAS_DISPLAY || !IS_SHUTTER_TESTER

void shutter_binding_init() {}

#endif

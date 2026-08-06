#include "shutter_session.h"

#if IS_SHUTTER_TESTER

#include "fs_indexed_store.h"
#include "shutter_capture.h"
#include "shutter_measure.h"
#include "shutter_session_actions.h"
#include "shutter_test_scripts.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "version.h"

#include "storage.h"
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <time.h>
#include <vector>

#define TAG "ShutterSession"

// ============================================================================
// FsIndexedStore configuration
// ============================================================================

static const char* kIndexFields[] = {
    "camera", "notes", "started_at", "ended_at", "count",
    "worst_verdict"
};
static const FsIndexedStoreRootField kRootFields[] = {
    {"next_id", FsIndexedStoreRootField::TYPE_UINT32, {.default_uint32 = 1}},
};

static FsIndexedStore s_store(
    "/storage/sessions",
    kIndexFields, sizeof(kIndexFields) / sizeof(kIndexFields[0]),
    kRootFields,  sizeof(kRootFields)  / sizeof(kRootFields[0])
);

// ============================================================================
// Module state — all protected by s_session_mutex
// ============================================================================

static SemaphoreHandle_t s_session_mutex = nullptr;

static bool     s_active       = false;
static uint32_t s_current_id   = 0;    // numeric id of the active session
static uint32_t s_next_id      = 1;    // cached next_id from manifest (bumped in memory at start)
static char     s_session_id[32] = {}; // string id (e.g. "sess_42")
static uint32_t s_started_at   = 0;    // unix timestamp
static char     s_camera[128]  = {};

// In-session measurement buffer (PSRAM vector).
// Each element owns its sensor[].waveform pointers via heap_caps_malloc.
static std::vector<ShutterSessionMeasurement>* s_measurements = nullptr;

// Snapshotted capture configuration at session start.
// Captured once when the session opens so meta reflects the active config
// during measurement, not whatever is active at save time.
static char     s_caps_preset_id_str[64] = {};
static uint8_t  s_session_caps_sensor_count      = 0;
static float    s_caps_sample_rate_hz    = 0.0f;

// Snapshotted sensor layout offsets at session start.
static float    s_caps_sensor_offset_x   = 0.0f;
static float    s_caps_sensor_offset_y   = 0.0f;
static float    s_caps_diagonal_mm       = 0.0f;

// Snapshotted per-sensor positions and topology at session start.
static ShutterSensorPosition s_caps_positions[SHUTTER_SENSOR_MAX] = {};
static uint8_t  s_caps_position_count    = 0;
static ShutterTopologyType s_caps_topology = ShutterTopologyType::ThreeLine;

// Guided session state
static bool     s_guided          = false;
static char     s_guide_id[SHUTTER_TEST_ID_MAX_LEN]   = {};
static char     s_guide_name[SHUTTER_TEST_NAME_MAX_LEN] = {};
static uint8_t  s_guide_shots_per = 1;
static uint16_t s_guide_step      = 0;    // current speed index (0-based)
static uint16_t s_guide_shot      = 0;    // shot counter within current speed (0-based)
static ShutterTestSpeed* s_guide_speeds = nullptr;  // PSRAM-allocated array
static uint16_t s_guide_speed_count = 0;

// ============================================================================
// Verdict helpers
// ============================================================================

static const char* verdict_str(uint8_t v) {
    if (v == SHUTTER_VERDICT_FAIL)    return "fail";
    if (v == SHUTTER_VERDICT_WARNING) return "warning";
    return "pass";
}

// Worst verdict across all measurements (0=pass < 1=warning < 2=fail)
static uint8_t worst_verdict(const std::vector<ShutterSessionMeasurement>& ms) {
    uint8_t w = SHUTTER_VERDICT_PASS;
    for (const auto& m : ms) {
        if (m.verdict > w) w = m.verdict;
    }
    return w;
}

// ============================================================================
// Waveform storage budget
// ============================================================================

// Fixed target sample count per stored sensor waveform. Constant by design:
// the portal expects a predictable visualization budget regardless of speed.
static const uint32_t WAVEFORM_STORAGE_TARGET_SAMPLES = 2000;

// ============================================================================
// Curtain statistics
// ============================================================================
//
// `compute_curtain_stats()` lives in shutter_curtain_stats.cpp so it can be
// unit-tested on the host without dragging in FreeRTOS / filesystem / ArduinoJson
// dependencies. The header is included transitively via shutter_session.h.

// ============================================================================
// Waveform downsampling — min-max per bucket
// ============================================================================

// Returns PSRAM-allocated buffer of length `*out_len`.
// Caller must heap_caps_free() the result when done.
// Returns nullptr if no samples or allocation fails.
static uint16_t* downsample_waveform(const uint16_t* samples, uint32_t count,
                                      uint32_t target, uint32_t* out_len) {
    if (!samples || count == 0) { *out_len = 0; return nullptr; }

    if (count <= target) {
        // No downsampling needed — just copy.
        uint16_t* buf = (uint16_t*)heap_caps_malloc(count * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!buf) { *out_len = 0; return nullptr; }
        memcpy(buf, samples, count * sizeof(uint16_t));
        *out_len = count;
        return buf;
    }

    // Average downsampling: divide input into `target` equal buckets, emit one
    // averaged value per bucket.  This gives maximum horizontal resolution for
    // the given storage budget.
    uint16_t* buf = (uint16_t*)heap_caps_malloc(target * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!buf) { *out_len = 0; return nullptr; }

    for (uint32_t b = 0; b < target; b++) {
        uint32_t start = (uint64_t)b * count / target;
        uint32_t end   = (uint64_t)(b + 1) * count / target;
        if (end > count) end = count;
        if (start >= end) { buf[b] = 0; continue; }

        uint32_t sum = 0;
        for (uint32_t i = start; i < end; i++) sum += samples[i];
        buf[b] = (uint16_t)(sum / (end - start));
    }
    *out_len = target;
    return buf;
}

// ============================================================================
// Snapshot helpers
// ============================================================================

static void free_measurement_waveforms(ShutterSessionMeasurement& m) {
    for (int i = 0; i < SHUTTER_SENSOR_MAX; i++) {
        if (m.sensors[i].waveform) {
            heap_caps_free(m.sensors[i].waveform);
            m.sensors[i].waveform = nullptr;
        }
    }
}

static ShutterSessionMeasurement snapshot_measurement(const ShutterMeasurement* m) {
    ShutterSessionMeasurement snap;
    memset(&snap, 0, sizeof(snap));

    snap.timestamp_ms       = m->timestamp_ms;
    strlcpy(snap.nearest_speed, m->nearest_speed, sizeof(snap.nearest_speed));
    snap.nearest_duration_ms = m->nearest_duration_ms;
    snap.avg_duration_ms     = m->avg_duration_ms;
    snap.deviation_pct       = m->deviation_pct;
    snap.deviation_stops     = m->deviation_stops;
    snap.verdict             = (uint8_t)m->verdict;
    snap.sensor_count        = m->sensor_count;
    snap.valid_sensor_count  = m->valid_sensor_count;
    snap.spread_pct          = m->spread_pct;
    snap.capping_gradient_stops_per_mm = m->capping_gradient_stops_per_mm;
    snap.capping_gradient_x_stops_per_mm = m->capping_gradient_x_stops_per_mm;
    snap.capping_gradient_y_stops_per_mm = m->capping_gradient_y_stops_per_mm;
    snap.skew_differential_us_per_mm = m->skew_differential_us_per_mm;
    snap.curtain1_skew_left_us  = m->curtain1_skew_left_us;
    snap.curtain1_skew_right_us = m->curtain1_skew_right_us;
    snap.curtain2_skew_left_us  = m->curtain2_skew_left_us;
    snap.curtain2_skew_right_us = m->curtain2_skew_right_us;
    strlcpy(snap.detected_travel, m->detected_travel, sizeof(snap.detected_travel));
    snap.target_manual       = m->target_manual;
    snap.speed_locked        = m->speed_locked;
    snap.sample_rate_hz      = 0.0f;  // filled below from capture frame

    for (int i = 0; i < m->sensor_count && i < SHUTTER_SENSOR_MAX; i++) {
        const ShutterSensorResult& sr = m->sensors[i];
        ShutterSessionSensor& ss = snap.sensors[i];
        ss.duration_ms      = sr.duration_ms;
        ss.min_adc          = sr.min_adc;
        ss.baseline_adc     = sr.baseline_adc;
        ss.threshold        = sr.threshold;
        ss.valid            = sr.valid;
        ss.waveform         = nullptr;
        ss.waveform_len     = 0;

        ss.trigger_frac     = 0.0f;
        ss.pulse_start_frac = 0.0f;
        ss.pulse_end_frac   = 0.0f;
    }

    // Copy waveforms from the latest capture frame (pointer valid now, stale after).
    // Trim each sensor's waveform to the zoomed viewport window: pulse_width/2
    // padding on each side (min 64 samples), same logic as waveform_widget.cpp.
    // This concentrates all stored points on the interesting region rather than
    // the silent pre/post buffer, giving much higher effective resolution.
    ShutterCaptureFrame frame;
    if (shutter_capture_get_latest(&frame) && frame.valid && frame.sensor_count > 0) {
        // Store sample rate from the capture frame.
        snap.sample_rate_hz = frame.waveforms[0].sample_rate_hz;

        // Fixed waveform storage budget per sensor (see WAVEFORM_STORAGE_TARGET_SAMPLES).
        uint32_t wf_target = WAVEFORM_STORAGE_TARGET_SAMPLES;

        // Compute view window across all sensors (consistent viewport).
        uint32_t total = (frame.sensor_count > 0) ? frame.waveforms[0].count : 0;
        uint32_t view_start = 0;
        uint32_t view_end   = total;
        if (total > 0) {
            uint32_t pulse_start = UINT32_MAX;
            uint32_t pulse_end   = 0;
            for (int i = 0; i < m->sensor_count && i < (int)frame.sensor_count; i++) {
                if (!m->sensors[i].valid) continue;
                if (m->sensors[i].start_idx < pulse_start) pulse_start = m->sensors[i].start_idx;
                if (m->sensors[i].end_idx   > pulse_end)   pulse_end   = m->sensors[i].end_idx;
            }
            if (pulse_start < pulse_end) {
                uint32_t pulse_width = pulse_end - pulse_start;
                uint32_t padding = pulse_width / 2;
                if (padding < 64) padding = 64;
                view_start = (pulse_start > padding) ? (pulse_start - padding) : 0;
                view_end   = pulse_end + padding;
                if (view_end > total) view_end = total;
            }
        }

        for (int i = 0; i < (int)frame.sensor_count && i < m->sensor_count; i++) {
            const ShutterWaveformView& wv = frame.waveforms[i];
            uint32_t slice_start = (view_start < wv.count) ? view_start : 0;
            uint32_t slice_end   = (view_end   < wv.count) ? view_end   : wv.count;
            if (!wv.samples || slice_end <= slice_start) continue;
            const uint16_t* slice_ptr = wv.samples + slice_start;
            uint32_t slice_len = slice_end - slice_start;

            // Compute curtain statistics from the full-resolution slice.
            // Using the slice (not the full buffer) ensures edge detection is
            // bounded to the pulse region and fractions align with the chart.
            // Baseline is sourced from the measurement result (computed from
            // true pre-pulse samples in compute_sensor_duration), NOT from the
            // pulse-centered slice — see contract on compute_curtain_stats().
            if (m->sensors[i].valid) {
                snap.sensors[i].curtain_stats = compute_curtain_stats(
                    slice_ptr, slice_len, wv.sample_rate_hz,
                    (float)m->sensors[i].baseline_adc);
            }

            uint32_t dl = 0;
            snap.sensors[i].waveform = downsample_waveform(slice_ptr, slice_len, wf_target, &dl);
            snap.sensors[i].waveform_len = dl;


            // Compute marker fractions within the stored slice.
            float flen = (float)slice_len;
            // Trigger index (first threshold crossing in the full buffer).
            uint32_t trig = wv.trigger_index;
            snap.sensors[i].trigger_frac =
                (trig >= slice_start && slice_len > 0)
                    ? ((float)(trig - slice_start) / flen)
                    : 0.0f;
            // Pulse start/end from measurement result.
            const ShutterSensorResult& sr = m->sensors[i];
            snap.sensors[i].pulse_start_frac =
                (sr.valid && sr.start_idx >= slice_start && slice_len > 0)
                    ? ((float)(sr.start_idx - slice_start) / flen)
                    : 0.0f;
            snap.sensors[i].pulse_end_frac =
                (sr.valid && sr.end_idx >= slice_start && slice_len > 0)
                    ? ((float)(sr.end_idx - slice_start) / flen)
                    : 0.0f;
        }
    }

    return snap;
}

// ============================================================================
// Streaming JSON writer for session data
// ============================================================================

// Write a flat waveform array of raw samples: [v0,v1,...]
static void write_waveform_array(File& f, const uint16_t* data, uint32_t len) {
    f.write((const uint8_t*)"[", 1);
    char nbuf[8];
    for (uint32_t i = 0; i < len; i++) {
        int n = snprintf(nbuf, sizeof(nbuf), "%u", (unsigned)data[i]);
        f.write((const uint8_t*)nbuf, (size_t)n);
        if (i + 1 < len) f.write((const uint8_t*)",", 1);
    }
    f.write((const uint8_t*)"]", 1);
}

// Snapshot of all session state needed by the background persist task.
// Captured at stop() time so subsequent start()/stop() cycles cannot mutate
// the values out from under the in-flight persist.
struct PersistContext {
    char     session_id[32];
    uint32_t started_at;
    char     camera[128];
    std::vector<ShutterSessionMeasurement>* measurements;  // owned

    // Snapshotted capture configuration at session start
    char     preset_id_str[64];
    uint8_t  sensor_count;
    float    sample_rate_hz;

    // Snapshotted sensor layout offsets at session start
    float    sensor_offset_x_mm;
    float    sensor_offset_y_mm;
    float    diagonal_mm;

    // Snapshotted per-sensor positions and topology
    ShutterSensorPosition positions[SHUTTER_SENSOR_MAX];
    uint8_t  position_count;
    ShutterTopologyType topology;

    // Guided session metadata (empty/zero if freeform)
    bool     guided;
    char     guide_id[SHUTTER_TEST_ID_MAX_LEN];
    char     guide_name[SHUTTER_TEST_NAME_MAX_LEN];
    uint8_t  guide_shots_per;
    ShutterTestSpeed* guide_targets;    // PSRAM-allocated copy, owned
    uint16_t guide_target_count;
};

// Write the full session JSON to a persistent-storage file handle.
// Uses ArduinoJson for metadata objects and raw writes for waveform arrays.
static bool write_session_json(File& f,
                                const PersistContext& ctx,
                                uint32_t ended_at,
                                const std::vector<ShutterSessionMeasurement>& ms) {
    // Outer object header
    {
        BasicJsonDocument<PsramJsonAllocator> hdr(2048);
        hdr["id"]         = ctx.session_id;
        hdr["type"]       = ctx.guided ? "guided" : "freeform";
        hdr["started_at"] = ctx.started_at;
        hdr["ended_at"]   = ended_at;
        hdr["count"]      = (uint32_t)ms.size();
        hdr["worst_verdict"] = verdict_str(worst_verdict(ms));

        if (ctx.guided) {
            hdr["template_id"]     = ctx.guide_id;
            hdr["template_name"]   = ctx.guide_name;
            hdr["shots_per_speed"] = ctx.guide_shots_per;
            JsonArray targets = hdr.createNestedArray("guided_targets");
            for (uint16_t i = 0; i < ctx.guide_target_count; i++) {
                targets.add(ctx.guide_targets[i].speed);
            }
        }

        // Meta context array — self-describing entries for portal rendering
        JsonArray meta = hdr.createNestedArray("meta");

        auto addMetaStr = [&](const char* key, const char* label, const char* value, const char* icon = nullptr) {
            JsonObject obj = meta.createNestedObject();
            obj["key"] = key; obj["label"] = label; obj["value"] = value;
            if (icon) obj["icon"] = icon;
        };

        addMetaStr("sensor_preset", "Sensor Preset", ctx.preset_id_str, "tune");

        { JsonObject obj = meta.createNestedObject(); obj["key"] = "active_sensors"; obj["label"] = "Active Sensors"; obj["value"] = ctx.sensor_count; obj["icon"] = "sensors"; }

        addMetaStr("firmware", "Tester Firmware", FIRMWARE_VERSION, "memory");

        { JsonObject obj = meta.createNestedObject(); obj["key"] = "sample_rate"; obj["label"] = "ADC Sample Rate"; obj["value"] = ctx.sample_rate_hz; obj["icon"] = "speed"; }

        // Ambient baseline — average baseline_adc from first measurement's valid sensors
        if (!ms.empty()) {
            uint32_t sum = 0; uint8_t cnt = 0;
            const auto& first = ms[0];
            for (int i = 0; i < first.sensor_count && i < SHUTTER_SENSOR_MAX; i++) {
                if (first.sensors[i].valid) { sum += first.sensors[i].baseline_adc; cnt++; }
            }
            if (cnt > 0) {
                JsonObject obj = meta.createNestedObject();
                obj["key"] = "ambient_baseline"; obj["label"] = "Ambient Baseline"; obj["value"] = (uint32_t)(sum / cnt); obj["icon"] = "brightness_auto";
            }
        }

        // Conditional: guided test
        if (ctx.guided) {
            addMetaStr("guided_test", "Guided Test", ctx.guide_name, "auto_fix_high");
        }

        // Sensor layout offsets (legacy 3-line "X/Y from centre sensor" display).
        // Only emit when the legacy offset fields are populated — multi-corner
        // presets (e.g. 4-corner) leave these at 0 and rely on the
        // "Sensor Positions" row below for their geometry.
        if (ctx.sensor_offset_x_mm > 0.0f || ctx.sensor_offset_y_mm > 0.0f) {
            char offset_val[32];
            snprintf(offset_val, sizeof(offset_val), "\xc2\xb1(%.1f, %.1f) mm",
                     ctx.sensor_offset_x_mm, ctx.sensor_offset_y_mm);
            JsonObject obj = meta.createNestedObject();
            obj["key"] = "sensor_offset";
            obj["label"] = "Sensor Offset";
            obj["value"].set(String(offset_val));
            obj["icon"] = "straighten";
            obj["sensor_offset_x_mm"] = ctx.sensor_offset_x_mm;
            obj["sensor_offset_y_mm"] = ctx.sensor_offset_y_mm;
            obj["sensor_diagonal_mm"] = ctx.diagonal_mm;
        }

        // Per-sensor positions (when geometry is configured)
        if (ctx.position_count > 0) {
            JsonObject pos_obj = meta.createNestedObject();
            pos_obj["key"] = "sensor_positions";
            pos_obj["label"] = "Sensor Positions";
            pos_obj["value"] = String(ctx.position_count) + " sensors";
            pos_obj["icon"] = "grid_on";
            JsonArray pos_list = pos_obj.createNestedArray("positions");
            for (int i = 0; i < ctx.position_count; i++) {
                JsonObject p = pos_list.createNestedObject();
                p["sensor"] = String("S") + String(i + 1);
                p["x_mm"] = ctx.positions[i].x_mm;
                p["y_mm"] = ctx.positions[i].y_mm;
            }
        }

        // Sensor topology
        {
            const char* topo_str = "unknown";
            switch (ctx.topology) {
                case ShutterTopologyType::SingleSensor: topo_str = "single"; break;
                case ShutterTopologyType::ThreeLine:    topo_str = "three_line"; break;
                case ShutterTopologyType::FourSensor:   topo_str = "four_sensor"; break;
                case ShutterTopologyType::Matrix3x3:    topo_str = "matrix_3x3"; break;
            }
            addMetaStr("topology", "Sensor Topology", topo_str, "grid_view");
        }

        // Detected travel direction — majority vote across measurements.
        if (!ms.empty()) {
            int v_count = 0, h_count = 0, l_count = 0;
            for (const auto& meas : ms) {
                if (meas.detected_travel[0] == 'V') v_count++;
                else if (meas.detected_travel[0] == 'H') h_count++;
                else if (meas.detected_travel[0] == 'L') l_count++;
            }
            const char* travel_str = nullptr;
            if (v_count > h_count && v_count > l_count) travel_str = "V";
            else if (h_count > v_count && h_count > l_count) travel_str = "H";
            else if (l_count > v_count && l_count > h_count) travel_str = "L";
            if (travel_str) {
                addMetaStr("detected_travel", "Detected Travel", travel_str, "swap_vert");
            }
        }

        // Serialize without closing brace — we'll append "measurements" manually
        String hdr_str;
        serializeJson(hdr, hdr_str);
        // Strip trailing "}" to append measurements array
        if (hdr_str.endsWith("}")) hdr_str.remove(hdr_str.length() - 1);
        f.print(hdr_str);
    }

    // measurements array
    f.write((const uint8_t*)",\"measurements\":[", 17);

    for (size_t mi = 0; mi < ms.size(); mi++) {
        const ShutterSessionMeasurement& m = ms[mi];
        if (mi > 0) f.write((const uint8_t*)",", 1);

        // Measurement metadata (no waveforms yet)
        BasicJsonDocument<PsramJsonAllocator> mdoc(1024);
        mdoc["timestamp_ms"]       = m.timestamp_ms;
        mdoc["nearest_speed"]      = m.nearest_speed;
        if (m.target_speed[0]) mdoc["target_speed"] = m.target_speed;
        mdoc["nearest_duration_ms"] = m.nearest_duration_ms;
        mdoc["avg_duration_ms"]    = m.avg_duration_ms;
        mdoc["deviation_pct"]      = m.deviation_pct;
        mdoc["deviation_stops"]    = m.deviation_stops;
        mdoc["verdict"]            = verdict_str(m.verdict);
        mdoc["sensor_count"]       = m.sensor_count;
        mdoc["valid_sensor_count"] = m.valid_sensor_count;
        mdoc["spread_pct"]         = m.spread_pct;
        if (m.capping_gradient_stops_per_mm >= 0.0f) {
            mdoc["capping_gradient_stops_per_mm"] = m.capping_gradient_stops_per_mm;
        }
        if (m.capping_gradient_x_stops_per_mm >= 0.0f) {
            mdoc["capping_gradient_x"] = m.capping_gradient_x_stops_per_mm;
        }
        if (m.capping_gradient_y_stops_per_mm >= 0.0f) {
            mdoc["capping_gradient_y"] = m.capping_gradient_y_stops_per_mm;
        }
        if (m.capping_gradient_x_stops_per_mm >= 0.0f) {  // skew fields are always computed when 2D capping is valid
            mdoc["skew_differential"] = m.skew_differential_us_per_mm;
            mdoc["curtain1_skew_left_us"]  = m.curtain1_skew_left_us;
            mdoc["curtain1_skew_right_us"] = m.curtain1_skew_right_us;
            mdoc["curtain2_skew_left_us"]  = m.curtain2_skew_left_us;
            mdoc["curtain2_skew_right_us"] = m.curtain2_skew_right_us;
        }
        if (m.detected_travel[0]) {
            mdoc["detected_travel"] = m.detected_travel;
        }
        mdoc["sample_rate_hz"]     = m.sample_rate_hz;
        mdoc["target_manual"]      = m.target_manual;
        mdoc["speed_locked"]       = m.speed_locked;
        // Stored waveform length (first valid sensor); lets JS know resolution.
        for (int wi = 0; wi < m.sensor_count && wi < SHUTTER_SENSOR_MAX; wi++) {
            if (m.sensors[wi].waveform_len > 0) {
                mdoc["waveform_len"] = m.sensors[wi].waveform_len;
                break;
            }
        }

        String mdoc_str;
        serializeJson(mdoc, mdoc_str);
        // Strip trailing "}" to append sensors array
        if (mdoc_str.endsWith("}")) mdoc_str.remove(mdoc_str.length() - 1);
        f.print(mdoc_str);

        // sensors array
        f.write((const uint8_t*)",\"sensors\":[", 12);
        for (int si = 0; si < m.sensor_count && si < SHUTTER_SENSOR_MAX; si++) {
            const ShutterSessionSensor& s = m.sensors[si];
            if (si > 0) f.write((const uint8_t*)",", 1);

            BasicJsonDocument<PsramJsonAllocator> sdoc(384);
            sdoc["duration_ms"]       = s.duration_ms;
            sdoc["min_adc"]           = s.min_adc;
            sdoc["baseline_adc"]      = s.baseline_adc;
            sdoc["threshold"]         = s.threshold;
            sdoc["valid"]             = s.valid;
            sdoc["trigger_frac"]         = s.trigger_frac;
            sdoc["pulse_start_frac"]     = s.pulse_start_frac;
            sdoc["pulse_end_frac"]       = s.pulse_end_frac;

            // Pre-computed curtain statistics.
            // Object is emitted whenever edges were detected. `valid` reflects
            // physical meaningfulness of `curtain_ratio` (false in full-open
            // mode or when close-edge scan ran into sensor recovery tail).
            // Timing fields (curtain1_ms / dwell_ms / curtain2_ms) remain
            // populated regardless of `valid` for diagnostic transparency.
            if (s.curtain_stats.edges_detected) {
                JsonObject cs = sdoc.createNestedObject("curtain_stats");
                cs["valid"]              = s.curtain_stats.valid;
                cs["curtain1_ms"]        = s.curtain_stats.curtain1_ms;
                cs["dwell_ms"]           = s.curtain_stats.dwell_ms;
                cs["curtain2_ms"]        = s.curtain_stats.curtain2_ms;
                cs["curtain_ratio"]      = s.curtain_stats.curtain_ratio;
                cs["curtain1_start_frac"] = s.curtain_stats.curtain1_start_frac;
                cs["curtain1_end_frac"]   = s.curtain_stats.curtain1_end_frac;
                cs["curtain1_mid_frac"]   = s.curtain_stats.curtain1_mid_frac;
                cs["curtain2_start_frac"] = s.curtain_stats.curtain2_start_frac;
                cs["curtain2_end_frac"]   = s.curtain_stats.curtain2_end_frac;
                cs["curtain2_mid_frac"]   = s.curtain_stats.curtain2_mid_frac;
            } else {
                sdoc["curtain_stats"] = nullptr;
            }

            String sdoc_str;
            serializeJson(sdoc, sdoc_str);
            // Strip "}" to append waveform
            if (sdoc_str.endsWith("}")) sdoc_str.remove(sdoc_str.length() - 1);
            f.print(sdoc_str);

            // waveform — flat array [v,...]
            f.write((const uint8_t*)",\"waveform\":", 12);
            if (s.waveform && s.waveform_len > 0) {
                write_waveform_array(f, s.waveform, s.waveform_len);
            } else {
                f.write((const uint8_t*)"[]", 2);
            }
            f.write((const uint8_t*)"}", 1);
        }
        f.write((const uint8_t*)"]}",  2);  // close sensors array + measurement object
    }

    f.write((const uint8_t*)"]}", 2);  // close measurements array + root object
    return true;
}

// ============================================================================
// Session persistence
// ============================================================================

static bool persist_session(const PersistContext& ctx) {
    if (!ctx.measurements || ctx.measurements->empty()) {
        LOGW(TAG, "persist_session: no measurements to save");
        return false;
    }

    uint32_t ended_at = (uint32_t)time(nullptr);
    if (ended_at < 1000000UL) ended_at = (uint32_t)(millis() / 1000);

    String data_path = s_store.data_path(ctx.session_id);
    String tmp_path  = data_path + ".tmp";

    // Write data file (streaming)
    File f = Storage.open(tmp_path.c_str(), "w");
    if (!f) {
        LOGE(TAG, "Cannot open %s for writing", tmp_path.c_str());
        return false;
    }

    bool ok = write_session_json(f, ctx, ended_at, *ctx.measurements);
    f.close();

    if (!ok) {
        Storage.remove(tmp_path.c_str());
        return false;
    }

    // Both supported filesystem backends fail rename() if the destination already
    // exists. A stale data file can be left behind when s_next_id was
    // bumped in memory but not yet persisted to disk before a reboot,
    // causing a later session to reuse the same id. Remove any pre-existing
    // file at the target path so the rename is idempotent on both backends.
    if (Storage.exists(data_path.c_str())) {
        LOGW(TAG, "Removing stale %s before rename", data_path.c_str());
        Storage.remove(data_path.c_str());
    }

    if (!Storage.rename(tmp_path.c_str(), data_path.c_str())) {
        LOGE(TAG, "Rename %s -> %s failed", tmp_path.c_str(), data_path.c_str());
        Storage.remove(tmp_path.c_str());
        return false;
    }

    // Register in manifest (data file already written above).
    BasicJsonDocument<PsramJsonAllocator> meta(512);
    meta["camera"]     = ctx.camera;
    meta["notes"]      = "";
    meta["started_at"] = ctx.started_at;
    meta["ended_at"]   = ended_at;
    meta["count"]      = (uint32_t)ctx.measurements->size();
    meta["worst_verdict"] = verdict_str(worst_verdict(*ctx.measurements));
    meta["type"].set(String(ctx.guided ? "guided" : "freeform"));
    if (ctx.guided) {
        meta["template_name"].set(String(ctx.guide_name));
    }

    if (!s_store.register_pre_written(ctx.session_id, ctx.started_at, meta.as<JsonObject>())) {
        LOGE(TAG, "register_pre_written failed for %s", ctx.session_id);
        return false;
    }

    // Persist the in-memory next_id to disk. This runs on an internal-RAM
    // stack task so flash writes are safe. Reading under the session mutex
    // ensures we persist the highest reserved value even if another session
    // has started in the meantime.
    uint32_t to_persist = 0;
    if (s_session_mutex && xSemaphoreTake(s_session_mutex, portMAX_DELAY) == pdTRUE) {
        to_persist = s_next_id;
        xSemaphoreGive(s_session_mutex);
    }
    if (to_persist != 0) {
        s_store.set_root_uint32("next_id", to_persist);
    }

    LOGI(TAG, "Session %s saved (%u measurements)", ctx.session_id,
         (unsigned)ctx.measurements->size());
    return true;
}

// ============================================================================
// Public API
// ============================================================================

FsIndexedStore& shutter_session_get_store() {
    return s_store;
}

void shutter_session_init() {
    s_session_mutex = xSemaphoreCreateMutex();
    if (!s_session_mutex) {
        LOGE(TAG, "Mutex creation failed");
        return;
    }
    // Ensure parent directory exists — Storage mkdir() is not recursive.
    // (Also pre-created by pad_config_init() on SD builds; defensive on flash storage.)
    if (!Storage.exists("/storage")) {
        Storage.mkdir("/storage");
    }
    if (!s_store.begin()) {
        LOGE(TAG, "FsIndexedStore begin() failed");
        return;
    }
    s_store.get_root_uint32("next_id", s_next_id);
    LOGI(TAG, "Init OK (next_id=%lu)", (unsigned long)s_next_id);
}

void shutter_session_start(const char* camera) {
    if (!s_session_mutex) return;

    // Acquire the ADC engine before doing any session bookkeeping. This is
    // the call that allocates the ~28 KB of DMA-internal RAM when no other
    // consumer (alignment, on-screen pad) is already holding the engine.
    // Fail-fast: if the engine cannot be brought up, do not enter the
    // active state — callers and bindings will see s_active stay false.
    if (!shutter_capture_acquire("session")) {
        LOGE(TAG, "start: failed to acquire capture engine");
        return;
    }

    // Snapshot capture configuration before acquiring the mutex to avoid
    // holding s_session_mutex while calling into the capture module (which may
    // have its own synchronization).
    ShutterCaptureCaps caps;
    shutter_capture_get_caps(&caps);

    if (xSemaphoreTake(s_session_mutex, portMAX_DELAY) != pdTRUE) {
        shutter_capture_release("session");
        return;
    }

    if (s_active) {
        xSemaphoreGive(s_session_mutex);
        shutter_capture_release("session");
        LOGW(TAG, "start: session already active");
        return;
    }

    // Allocate PSRAM vector for measurements
    s_measurements = new std::vector<ShutterSessionMeasurement>();

    // Reserve the next id in memory only — no disk I/O here because this
    // function runs on the LVGL task which has a PSRAM stack (flash writes
    // disable the cache, crashing PSRAM-stack tasks).  The bumped value is
    // persisted to disk later by the background persist task.
    s_current_id = s_next_id;
    s_next_id    = s_current_id + 1;
    snprintf(s_session_id, sizeof(s_session_id), "sess_%lu", (unsigned long)s_current_id);

    s_started_at = (uint32_t)time(nullptr);
    if (s_started_at < 1000000UL) s_started_at = (uint32_t)(millis() / 1000);

    strlcpy(s_camera, camera ? camera : "", sizeof(s_camera));
    s_active = true;

    // Copy pre-snapshotted capture configuration into session statics
    strlcpy(s_caps_preset_id_str, caps.preset_id_str ? caps.preset_id_str : "", sizeof(s_caps_preset_id_str));
    s_session_caps_sensor_count   = caps.sensor_count;
    s_caps_sample_rate_hz = (float)caps.sample_rate_hz_per_sensor;

    // Snapshot sensor layout offsets from measurement engine
    shutter_measure_get_sensor_offsets(&s_caps_sensor_offset_x, &s_caps_sensor_offset_y, &s_caps_diagonal_mm);

    // Snapshot per-sensor positions and topology
    s_caps_position_count = shutter_measure_get_geometry(s_caps_positions, SHUTTER_SENSOR_MAX);
    s_caps_topology = caps.topology;

    xSemaphoreGive(s_session_mutex);
    LOGI(TAG, "Session %s started (camera='%s')", s_session_id, s_camera);
}

void shutter_session_stop() {
    if (!s_session_mutex) return;
    if (xSemaphoreTake(s_session_mutex, portMAX_DELAY) != pdTRUE) return;

    if (!s_active) {
        xSemaphoreGive(s_session_mutex);
        return;
    }

    s_active = false;

    // Snapshot all per-session state under the mutex. Globals (s_session_id,
    // s_started_at, s_camera, s_measurements) may be reused by a subsequent
    // start() before the background persist task runs.
    PersistContext* ctx = new PersistContext();
    strlcpy(ctx->session_id, s_session_id, sizeof(ctx->session_id));
    ctx->started_at   = s_started_at;
    strlcpy(ctx->camera, s_camera, sizeof(ctx->camera));
    ctx->measurements = s_measurements;
    s_measurements    = nullptr;  // ownership transferred to ctx

    // Copy snapshotted capture configuration
    strlcpy(ctx->preset_id_str, s_caps_preset_id_str, sizeof(ctx->preset_id_str));
    ctx->sensor_count   = s_session_caps_sensor_count;
    ctx->sample_rate_hz = s_caps_sample_rate_hz;

    // Copy snapshotted sensor layout offsets
    ctx->sensor_offset_x_mm = s_caps_sensor_offset_x;
    ctx->sensor_offset_y_mm = s_caps_sensor_offset_y;
    ctx->diagonal_mm        = s_caps_diagonal_mm;

    // Copy snapshotted per-sensor positions and topology
    memcpy(ctx->positions, s_caps_positions, sizeof(s_caps_positions));
    ctx->position_count = s_caps_position_count;
    ctx->topology       = s_caps_topology;

    // Capture guided metadata BEFORE clearing guided state
    ctx->guided = s_guided;
    if (s_guided) {
        strlcpy(ctx->guide_id, s_guide_id, sizeof(ctx->guide_id));
        strlcpy(ctx->guide_name, s_guide_name, sizeof(ctx->guide_name));
        ctx->guide_shots_per    = s_guide_shots_per;
        ctx->guide_targets      = s_guide_speeds;   // transfer ownership
        ctx->guide_target_count = s_guide_speed_count;
        s_guide_speeds      = nullptr;  // ownership transferred
        s_guide_speed_count = 0;
    } else {
        ctx->guide_id[0]        = '\0';
        ctx->guide_name[0]      = '\0';
        ctx->guide_shots_per    = 0;
        ctx->guide_targets      = nullptr;
        ctx->guide_target_count = 0;
    }

    // Clear guided state
    s_guided = false;
    s_guide_id[0] = '\0';
    s_guide_name[0] = '\0';
    s_guide_shots_per = 1;
    s_guide_step = 0;
    s_guide_shot = 0;

    xSemaphoreGive(s_session_mutex);

    // If a guided session was active, release the measurement-engine lock that
    // shutter_session_guide_start() asserted. Without this, the lock outlives
    // the guided session and any subsequent freeform session stays pinned to
    // the last guided target speed. Done outside the mutex because the
    // measurement engine has its own internal lock.
    if (ctx->guided) {
        shutter_measure_set_lock(false);
    }

    // Fire the user-configured "save started" lifecycle action (e.g. notify
    // bubble, screen navigation, MQTT publish). Dispatched AFTER releasing
    // s_session_mutex so an action handler that indirectly re-enters the session
    // module cannot deadlock. Runs on the LVGL/action-dispatch task — safe
    // to call action_dispatch() directly. No-op when unconfigured.
    shutter_session_actions_dispatch_start();

    // persist_session() performs persistent filesystem I/O, which can disable the flash cache.
    // The calling task (LVGL / action dispatch) has a PSRAM stack, which is
    // illegal when the cache is disabled.  Offload to a short-lived task
    // created with xTaskCreate so its stack lands in internal RAM.
    auto persist_and_free = [](void* arg) {
        PersistContext* c = static_cast<PersistContext*>(arg);
        persist_session(*c);
        if (c->measurements) {
            for (auto& m : *c->measurements) free_measurement_waveforms(m);
            delete c->measurements;
        }
        if (c->guide_targets) heap_caps_free(c->guide_targets);
        delete c;
        // Signal the LVGL task to dispatch the "save complete" lifecycle action.
        // Cannot call action_dispatch() here: this task has an internal-RAM stack
        // but no LVGL access, and dispatch may touch widgets / network / etc.
        shutter_session_actions_notify_complete();
        vTaskDelete(nullptr);
    };
    if (xTaskCreate(persist_and_free, "sess_persist", 8192, ctx, 5, nullptr) != pdPASS) {
        LOGE(TAG, "Failed to create persist task — freeing without save");
        if (ctx->measurements) {
            for (auto& m : *ctx->measurements) free_measurement_waveforms(m);
            delete ctx->measurements;
        }
        if (ctx->guide_targets) heap_caps_free(ctx->guide_targets);
        delete ctx;
    }

    // Release the engine refcount now that the session is no longer using
    // live samples. The persist task only writes recorded waveform data to
    // disk and does not need ADC. If another consumer (alignment, pad
    // screen) still holds the engine, it will remain running.
    shutter_capture_release("session");
}

void shutter_session_toggle(const char* camera) {
    if (!s_session_mutex) return;
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    bool active = s_active;
    xSemaphoreGive(s_session_mutex);

    if (active) {
        shutter_session_stop();
    } else {
        shutter_session_start(camera);
    }
}

void shutter_session_discard_last() {
    if (!s_session_mutex) return;
    if (xSemaphoreTake(s_session_mutex, portMAX_DELAY) != pdTRUE) return;

    if (!s_active || !s_measurements || s_measurements->empty()) {
        xSemaphoreGive(s_session_mutex);
        return;
    }

    free_measurement_waveforms(s_measurements->back());
    s_measurements->pop_back();

    xSemaphoreGive(s_session_mutex);
    LOGI(TAG, "Discarded last measurement (%u remaining)", (unsigned)s_measurements->size());
}

bool shutter_session_is_active() {
    if (!s_session_mutex) return false;
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    bool a = s_active;
    xSemaphoreGive(s_session_mutex);
    return a;
}

uint32_t shutter_session_get_count() {
    if (!s_session_mutex) return 0;
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    uint32_t c = (s_active && s_measurements) ? (uint32_t)s_measurements->size() : 0;
    xSemaphoreGive(s_session_mutex);
    return c;
}

uint32_t shutter_session_get_id() {
    if (!s_session_mutex) return 0;
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    uint32_t id = s_active ? s_current_id : 0;
    xSemaphoreGive(s_session_mutex);
    return id;
}

void shutter_session_on_measurement(const ShutterMeasurement* m) {
    if (!m || !m->valid) return;
    if (!s_session_mutex) return;
    if (xSemaphoreTake(s_session_mutex, portMAX_DELAY) != pdTRUE) return;

    if (!s_active || !s_measurements) {
        xSemaphoreGive(s_session_mutex);
        return;
    }

    // snapshot_measurement() calls shutter_capture_get_latest() which is safe here:
    // capture task writes to a double-buffered frame, get_latest() copies atomically.
    ShutterSessionMeasurement snap = snapshot_measurement(m);

    // Record the guided target speed for this measurement
    if (s_guided && s_guide_speeds && s_guide_step < s_guide_speed_count) {
        strlcpy(snap.target_speed, s_guide_speeds[s_guide_step].speed_suffixed,
                sizeof(snap.target_speed));
    }

    s_measurements->push_back(snap);

    // Guided auto-advance: increment shot counter, advance speed if needed
    bool do_auto_stop = false;
    bool do_advance = false;
    char next_speed[16] = {};
    if (s_guided && s_guide_speeds) {
        s_guide_shot++;
        if (s_guide_shot >= s_guide_shots_per) {
            s_guide_step++;
            s_guide_shot = 0;
            if (s_guide_step >= s_guide_speed_count) {
                do_auto_stop = true;
            } else {
                do_advance = true;
                strlcpy(next_speed, s_guide_speeds[s_guide_step].speed_suffixed,
                        sizeof(next_speed));
            }
        }
    }

    xSemaphoreGive(s_session_mutex);

    // Deferred operations outside mutex
    if (do_advance) {
        shutter_measure_set_target(next_speed, false);  // don't recompute last measurement
    }
    if (do_auto_stop) {
        LOGI(TAG, "Guided session complete — auto-stopping");
        shutter_session_stop();
    }
}

void shutter_session_on_recompute() {
    if (!s_session_mutex) return;
    if (xSemaphoreTake(s_session_mutex, portMAX_DELAY) != pdTRUE) return;

    if (!s_active || !s_measurements || s_measurements->empty()) {
        xSemaphoreGive(s_session_mutex);
        return;
    }

    // In guided mode, measurements are recorded with a fixed target speed.
    // Recompute would overwrite nearest_speed with the NEW target after
    // step-advance, corrupting the last measurement's grouping.
    if (s_guided) {
        xSemaphoreGive(s_session_mutex);
        return;
    }

    // Get the updated latest measurement and overwrite the last snapshot.
    ShutterMeasurement m;
    if (!shutter_measure_get_latest(&m) || !m.valid) {
        xSemaphoreGive(s_session_mutex);
        return;
    }

    ShutterSessionMeasurement& last = s_measurements->back();
    // Keep existing waveform buffers — recompute only changes metadata fields.
    // Free only if waveforms are about to be replaced (they won't be — recompute
    // does not generate new waveform data).
    strlcpy(last.nearest_speed, m.nearest_speed, sizeof(last.nearest_speed));
    last.nearest_duration_ms = m.nearest_duration_ms;
    last.avg_duration_ms     = m.avg_duration_ms;
    last.deviation_pct       = m.deviation_pct;
    last.deviation_stops     = m.deviation_stops;
    last.verdict             = (uint8_t)m.verdict;
    last.spread_pct          = m.spread_pct;
    last.capping_gradient_stops_per_mm = m.capping_gradient_stops_per_mm;
    last.target_manual       = m.target_manual;
    last.speed_locked        = m.speed_locked;

    xSemaphoreGive(s_session_mutex);
}

// ============================================================================
// Guided session API
// ============================================================================

// Inner implementation — runs on an internal-RAM-stack task because
// shutter_test_scripts_parse() performs filesystem I/O which can disable the SPI
// flash cache.  The calling LVGL/action-dispatch task has a PSRAM stack,
// which is illegal when the cache is disabled.
static void guide_start_inner(const char* test_id) {
    if (shutter_session_is_active()) {
        LOGW(TAG, "guide_start: session already active");
        return;
    }

    ShutterTestParseResult* result = (ShutterTestParseResult*)heap_caps_malloc(
        sizeof(ShutterTestParseResult), MALLOC_CAP_SPIRAM);
    if (!result) {
        LOGE(TAG, "guide_start: PSRAM alloc failed for parse result");
        return;
    }
    if (shutter_test_scripts_parse(result) == 0) {
        LOGW(TAG, "guide_start: no test scripts found");
        heap_caps_free(result);
        return;
    }

    const ShutterTestScript* script = shutter_test_scripts_find(result, test_id);
    if (!script) {
        LOGW(TAG, "guide_start: test '%s' not found", test_id);
        heap_caps_free(result);
        return;
    }

    if (script->speed_count == 0) {
        LOGW(TAG, "guide_start: test '%s' has no speeds", test_id);
        heap_caps_free(result);
        return;
    }

    // Allocate PSRAM copy of speed list
    size_t speeds_size = script->speed_count * sizeof(ShutterTestSpeed);
    ShutterTestSpeed* speeds_copy = (ShutterTestSpeed*)heap_caps_malloc(
        speeds_size, MALLOC_CAP_SPIRAM);
    if (!speeds_copy) {
        LOGE(TAG, "guide_start: PSRAM alloc failed (%u bytes)", (unsigned)speeds_size);
        heap_caps_free(result);
        return;
    }
    memcpy(speeds_copy, script->speeds, speeds_size);

    shutter_capture_stop_alignment();
    shutter_session_start(nullptr);

    if (xSemaphoreTake(s_session_mutex, portMAX_DELAY) != pdTRUE) {
        heap_caps_free(speeds_copy);
        heap_caps_free(result);
        return;
    }

    s_guided = true;
    strlcpy(s_guide_id, script->id, sizeof(s_guide_id));
    strlcpy(s_guide_name, script->name, sizeof(s_guide_name));
    s_guide_shots_per   = script->shots_per_speed;
    s_guide_speeds      = speeds_copy;
    s_guide_speed_count = script->speed_count;
    s_guide_step = 0;
    s_guide_shot = 0;

    xSemaphoreGive(s_session_mutex);

    shutter_measure_set_target(speeds_copy[0].speed_suffixed);
    shutter_measure_set_lock(true);

    LOGI(TAG, "Guided session started: '%s' (%u speeds, %u shots/speed)",
         script->name, (unsigned)script->speed_count, (unsigned)script->shots_per_speed);

    heap_caps_free(result);
}

void shutter_session_guide_start(const char* test_id) {
    if (!test_id || !test_id[0]) {
        LOGW(TAG, "guide_start: empty test id");
        return;
    }

    // Copy id so it survives across task boundary
    char* id_copy = strdup(test_id);
    if (!id_copy) return;

    auto task_fn = [](void* arg) {
        char* id = static_cast<char*>(arg);
        guide_start_inner(id);
        free(id);
        vTaskDelete(nullptr);
    };

    // xTaskCreate places stack in internal RAM — required for filesystem I/O
    if (xTaskCreate(task_fn, "guide_start", 4096, id_copy, 5, nullptr) != pdPASS) {
        LOGE(TAG, "guide_start: failed to create task");
        free(id_copy);
    }
}

void shutter_session_guide_stop() {
    shutter_session_stop();
}

void shutter_session_guide_skip() {
    if (!s_session_mutex) return;
    if (xSemaphoreTake(s_session_mutex, portMAX_DELAY) != pdTRUE) return;

    if (!s_active || !s_guided || !s_guide_speeds) {
        xSemaphoreGive(s_session_mutex);
        return;
    }

    bool do_auto_stop = false;
    char next_speed[16] = {};

    s_guide_step++;
    s_guide_shot = 0;

    if (s_guide_step >= s_guide_speed_count) {
        do_auto_stop = true;
    } else {
        strlcpy(next_speed, s_guide_speeds[s_guide_step].speed_suffixed,
                sizeof(next_speed));
    }

    xSemaphoreGive(s_session_mutex);

    if (do_auto_stop) {
        LOGI(TAG, "guide_skip: at last speed — auto-stopping");
        shutter_session_stop();
    } else {
        shutter_measure_set_target(next_speed, false);
        shutter_measure_set_lock(true);
        LOGI(TAG, "guide_skip: advanced to %s", next_speed);
    }
}

void shutter_session_guide_redo() {
    if (!s_session_mutex) return;
    if (xSemaphoreTake(s_session_mutex, portMAX_DELAY) != pdTRUE) return;

    if (!s_active || !s_guided) {
        xSemaphoreGive(s_session_mutex);
        return;
    }

    if (s_guide_shot == 0 && s_guide_step == 0) {
        xSemaphoreGive(s_session_mutex);
        LOGI(TAG, "guide_redo: at very first shot, no-op");
        return;
    }

    // Determine whether we need to roll back across a speed boundary
    bool crossed_boundary = (s_guide_shot == 0 && s_guide_step > 0);
    char prev_speed[16] = {};

    if (crossed_boundary) {
        s_guide_step--;
        s_guide_shot = s_guide_shots_per - 1;
        if (s_guide_speeds) {
            strlcpy(prev_speed, s_guide_speeds[s_guide_step].speed_suffixed,
                    sizeof(prev_speed));
        }
    } else {
        s_guide_shot--;
    }

    if (s_measurements && !s_measurements->empty()) {
        free_measurement_waveforms(s_measurements->back());
        s_measurements->pop_back();
    }

    xSemaphoreGive(s_session_mutex);

    if (crossed_boundary) {
        shutter_measure_set_target(prev_speed, false);
        LOGI(TAG, "guide_redo: rolled back to %s shot %u", prev_speed, (unsigned)s_guide_shot);
    } else {
        LOGI(TAG, "guide_redo: discarded last shot, now at shot %u", (unsigned)s_guide_shot);
    }
}

bool shutter_session_is_guided() {
    if (!s_session_mutex) return false;
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    bool g = s_active && s_guided;
    xSemaphoreGive(s_session_mutex);
    return g;
}

void shutter_session_get_type(char* out, size_t len) {
    if (!s_session_mutex) { out[0] = '\0'; return; }
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    if (!s_active) strlcpy(out, "", len);
    else if (s_guided) strlcpy(out, "guided", len);
    else strlcpy(out, "freeform", len);
    xSemaphoreGive(s_session_mutex);
}

void shutter_session_get_worst_verdict(char* out, size_t len) {
    if (!s_session_mutex) { out[0] = '\0'; return; }
    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(50)) != pdTRUE) { out[0] = '\0'; return; }
    if (!s_active || !s_measurements || s_measurements->empty()) {
        out[0] = '\0';
        xSemaphoreGive(s_session_mutex);
        return;
    }
    strlcpy(out, verdict_str(worst_verdict(*s_measurements)), len);
    xSemaphoreGive(s_session_mutex);
}

void shutter_session_guide_get_target(char* out, size_t len) {
    if (!s_session_mutex) { strlcpy(out, "---", len); return; }
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    if (s_active && s_guided && s_guide_speeds && s_guide_step < s_guide_speed_count)
        strlcpy(out, s_guide_speeds[s_guide_step].speed, len);
    else strlcpy(out, "---", len);
    xSemaphoreGive(s_session_mutex);
}

void shutter_session_guide_get_step(char* out, size_t len) {
    if (!s_session_mutex) { strlcpy(out, "---", len); return; }
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    if (s_active && s_guided) snprintf(out, len, "%u", (unsigned)(s_guide_step + 1));
    else strlcpy(out, "---", len);
    xSemaphoreGive(s_session_mutex);
}

void shutter_session_guide_get_steps(char* out, size_t len) {
    if (!s_session_mutex) { strlcpy(out, "---", len); return; }
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    if (s_active && s_guided) snprintf(out, len, "%u", (unsigned)s_guide_speed_count);
    else strlcpy(out, "---", len);
    xSemaphoreGive(s_session_mutex);
}

void shutter_session_guide_get_shot(char* out, size_t len) {
    if (!s_session_mutex) { strlcpy(out, "---", len); return; }
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    if (s_active && s_guided) snprintf(out, len, "%u", (unsigned)(s_guide_shot + 1));
    else strlcpy(out, "---", len);
    xSemaphoreGive(s_session_mutex);
}

void shutter_session_guide_get_shots(char* out, size_t len) {
    if (!s_session_mutex) { strlcpy(out, "---", len); return; }
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    if (s_active && s_guided) snprintf(out, len, "%u", (unsigned)s_guide_shots_per);
    else strlcpy(out, "---", len);
    xSemaphoreGive(s_session_mutex);
}

void shutter_session_guide_get_taking(char* out, size_t len) {
    if (!s_session_mutex) { strlcpy(out, "---", len); return; }
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    if (s_active && s_guided) {
        unsigned taking = s_guide_step * s_guide_shots_per + s_guide_shot + 1;
        snprintf(out, len, "%u", taking);
    } else strlcpy(out, "---", len);
    xSemaphoreGive(s_session_mutex);
}

void shutter_session_guide_get_total(char* out, size_t len) {
    if (!s_session_mutex) { strlcpy(out, "---", len); return; }
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    if (s_active && s_guided) snprintf(out, len, "%u", (unsigned)(s_guide_speed_count * s_guide_shots_per));
    else strlcpy(out, "---", len);
    xSemaphoreGive(s_session_mutex);
}

void shutter_session_guide_get_name(char* out, size_t len) {
    if (!s_session_mutex) { strlcpy(out, "---", len); return; }
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    if (s_active && s_guided) strlcpy(out, s_guide_name, len);
    else strlcpy(out, "---", len);
    xSemaphoreGive(s_session_mutex);
}

void shutter_session_guide_get_id(char* out, size_t len) {
    if (!s_session_mutex) { strlcpy(out, "---", len); return; }
    xSemaphoreTake(s_session_mutex, portMAX_DELAY);
    if (s_active && s_guided) strlcpy(out, s_guide_id, len);
    else strlcpy(out, "---", len);
    xSemaphoreGive(s_session_mutex);
}

#endif // IS_SHUTTER_TESTER

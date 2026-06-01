#include "test_strip.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "binding_template.h"
#include "config_manager.h"
#include "log_manager.h"
#include "print_log.h"
#include "relay_controller.h"

#if HAS_AUDIO
#include "audio.h"
#endif

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TAG "Strip"

// ============================================================================
// Constants
// ============================================================================

static constexpr int    STRIP_MAX_SEGMENTS     = 12;
static constexpr float  STRIP_BASE_TIME_MIN    = 1.0f;
static constexpr float  STRIP_BASE_TIME_MAX    = 999.9f;
static constexpr int    STRIP_SEGMENTS_MIN     = 3;
static constexpr int    STRIP_SEGMENTS_MAX     = 11;
static constexpr int    STRIP_COUNTDOWN_MIN    = 2;
static constexpr int    STRIP_COUNTDOWN_MAX    = 10;
static constexpr int    STRIP_PAUSE_MIN        = 3;
static constexpr int    STRIP_PAUSE_MAX        = 15;

// Pre-exposure beep lead time (fired during last 3s of countdown/pause)
static constexpr uint32_t PRE_EXPOSE_BEEP_LEAD_MS = 3000;

// Audio patterns (beep DSL)
static const char* AUDIO_TICK            = "600:80";
static const char* AUDIO_PRE_EXPOSE_BEEP = "700:200 800 900:200 800 1100:300";
static const char* AUDIO_SEGMENT_DONE    = "1000:100 100 1000:100 100 1000:100";
static const char* AUDIO_SEQUENCE_DONE   = "800:500 200 1200:500";
static const char* AUDIO_EXPOSE_TICK     = "600:50";

// Step interval table (fixed photographic fractions)
struct StepEntry {
    float   value;    // numeric value in stops
    const char* label; // display label (e.g. "1/3")
};
static constexpr StepEntry STEP_TABLE[] = {
    { 0.200f, "1/5" },
    { 0.250f, "1/4" },
    { 0.333f, "1/3" },
    { 0.500f, "1/2" },
    { 1.000f, "1/1" },
};
static constexpr int STEP_TABLE_SIZE = sizeof(STEP_TABLE) / sizeof(STEP_TABLE[0]);
static constexpr int STEP_DEFAULT_IDX = 2;  // 1/3 stop

// ============================================================================
// State
// ============================================================================

enum StripPhase : uint8_t {
    STRIP_IDLE = 0,
    STRIP_INITIAL_COUNTDOWN,
    STRIP_EXPOSING,
    STRIP_BETWEEN_SEGMENTS,
};

// Pre-calculated segment data
struct SegmentInfo {
    float cumulative_s;    // cumulative exposure time (seconds)
    float incremental_s;   // incremental time for this segment (seconds)
    float offset_stops;    // f-stop offset from base
};

static struct {
    // Configuration (modified via action commands)
    float  base_time_s;        // center exposure time (default 8.0)
    int    step_idx;           // index into STEP_TABLE (default STEP_DEFAULT_IDX = 1/3)
    int    segment_count;      // number of segments (default 7)
    int    countdown_s;        // initial countdown duration (default 5)
    int    pause_s;            // inter-segment pause duration (default 3)
    bool   exposure_tick;      // per-second tick during exposure (default true)

    // Runtime state
    StripPhase phase;
    int        current_segment;   // 1-based, current or just-completed
    uint32_t   phase_start_ms;    // millis() at phase start
    uint32_t   phase_duration_ms; // target duration for current phase

    // Pre-calculated segment table
    SegmentInfo segments[STRIP_MAX_SEGMENTS];

    // Audio tick tracking
    uint32_t   last_tick_ms;      // for per-second ticks
    bool       pre_beep_fired;    // pre-expose beep already played this phase
} g_strip = {
    .base_time_s     = 8.0f,
    .step_idx        = STEP_DEFAULT_IDX,
    .segment_count   = 7,
    .countdown_s     = 5,
    .pause_s         = 3,
    .exposure_tick   = true,
    .phase           = STRIP_IDLE,
    .current_segment = 0,
    .phase_start_ms  = 0,
    .phase_duration_ms = 0,
    .segments        = {},
    .last_tick_ms    = 0,
    .pre_beep_fired  = false,
};

// ============================================================================
// F-stop math
// ============================================================================

static float strip_snap_tenth(float v) {
    return roundf(v * 10.0f) / 10.0f;
}

static float current_step_value() {
    return STEP_TABLE[g_strip.step_idx].value;
}

static const char* current_step_label() {
    return STEP_TABLE[g_strip.step_idx].label;
}

static void recalculate_segments() {
    int n = g_strip.segment_count;
    float center = (n - 1) / 2.0f;
    float step = current_step_value();

    // Progressive uncover (left-to-right): index 0 = most light (highest
    // cumulative), index n-1 = least light.  Reverse the offset so the
    // descending cumulative order matches the physical uncover sequence.
    for (int i = 0; i < n; i++) {
        float offset = (center - i) * step;          // descending
        float cum = strip_snap_tenth(g_strip.base_time_s * powf(2.0f, offset));
        if (cum < 0.1f) cum = 0.1f;

        g_strip.segments[i].cumulative_s = cum;
        g_strip.segments[i].offset_stops = offset;

        if (i == n - 1) {
            // Last segment (least light): incremental == cumulative
            g_strip.segments[i].incremental_s = cum;
        } else {
            // Incremental = difference to next (smaller) cumulative
            float next_cum = strip_snap_tenth(g_strip.base_time_s * powf(2.0f, (center - (i + 1)) * step));
            if (next_cum < 0.1f) next_cum = 0.1f;
            float inc = cum - next_cum;
            if (inc < 0.1f) inc = 0.1f;
            g_strip.segments[i].incremental_s = strip_snap_tenth(inc);
        }
    }
}

// ============================================================================
// Timer format helper
// ============================================================================

static int strip_format_ms(uint32_t ms, const char* fmt, char* out, size_t out_len) {
    if (!fmt || !fmt[0]) fmt = "mm:ss";

    uint32_t total_sec = ms / 1000;
    uint32_t frac_ms   = ms % 1000;
    uint32_t h = total_sec / 3600;
    uint32_t m = (total_sec % 3600) / 60;
    uint32_t sec = total_sec % 60;
    uint32_t deci = frac_ms / 100;

    if (strcmp(fmt, "hh:mm:ss") == 0)
        return snprintf(out, out_len, "%u:%02u:%02u", h, m, sec);
    if (strcmp(fmt, "ss") == 0)
        return snprintf(out, out_len, "%u", (unsigned)(ms / 1000));
    if (strcmp(fmt, "mm:ss.d") == 0)
        return snprintf(out, out_len, "%u:%02u.%u", m, sec, deci);
    if (strcmp(fmt, "ss.d") == 0)
        return snprintf(out, out_len, "%u.%u", (unsigned)total_sec, deci);
    // default mm:ss
    return snprintf(out, out_len, "%u:%02u", m, sec);
}

// ============================================================================
// Phase elapsed/remaining helpers
// ============================================================================

static uint32_t phase_elapsed_ms() {
    if (g_strip.phase == STRIP_IDLE) return 0;
    return millis() - g_strip.phase_start_ms;
}

static uint32_t phase_remaining_ms() {
    if (g_strip.phase == STRIP_IDLE) return 0;
    uint32_t elapsed = phase_elapsed_ms();
    return (elapsed >= g_strip.phase_duration_ms) ? 0 : (g_strip.phase_duration_ms - elapsed);
}

// ============================================================================
// Total sequence time estimation
// ============================================================================

static float estimate_total_time_s() {
    int n = g_strip.segment_count;
    // Sum of all segment exposure durations
    float sum = 0;
    for (int i = 0; i < n; i++) {
        sum += g_strip.segments[i].incremental_s;
    }
    // Add pauses between segments (N-1)
    sum += (n - 1) * g_strip.pause_s;
    // Add initial countdown
    sum += g_strip.countdown_s;
    return sum;
}

// ============================================================================
// Phase transitions
// ============================================================================

static void enter_phase(StripPhase phase, uint32_t duration_ms) {
    g_strip.phase = phase;
    g_strip.phase_start_ms = millis();
    g_strip.phase_duration_ms = duration_ms;
    g_strip.last_tick_ms = 0;
    g_strip.pre_beep_fired = false;
}

static void start_sequence() {
    if (g_strip.phase != STRIP_IDLE) return;

    recalculate_segments();
    g_strip.current_segment = 0;

    print_log_clear_id();

    LOGI(TAG, "Start: %d segs, base %.1fs, step %s stops, countdown %ds, pause %ds",
         g_strip.segment_count, g_strip.base_time_s, current_step_label(), g_strip.countdown_s, g_strip.pause_s);

    // Log segment table
    for (int i = 0; i < g_strip.segment_count; i++) {
        LOGI(TAG, "  Seg %d: cum=%.1fs inc=%.1fs offset=%+.3f stops",
             i + 1, g_strip.segments[i].cumulative_s, g_strip.segments[i].incremental_s,
             g_strip.segments[i].offset_stops);
    }

    enter_phase(STRIP_INITIAL_COUNTDOWN, g_strip.countdown_s * 1000U);
}

static void start_exposing() {
    g_strip.current_segment++;
    int idx = g_strip.current_segment - 1;
    uint32_t dur_ms = (uint32_t)(g_strip.segments[idx].incremental_s * 1000.0f);

    enter_phase(STRIP_EXPOSING, dur_ms);
    relay_request(true);

    LOGI(TAG, "Exposing seg %d/%d: %.1fs (cum %.1fs)",
         g_strip.current_segment, g_strip.segment_count,
         g_strip.segments[idx].incremental_s, g_strip.segments[idx].cumulative_s);
}

static void start_between_segments() {
    enter_phase(STRIP_BETWEEN_SEGMENTS, g_strip.pause_s * 1000U);
    relay_request(false);

#if HAS_AUDIO
    audio_beep(AUDIO_SEGMENT_DONE, 0);
#endif

    LOGI(TAG, "Between segments: pause %ds, next seg %d",
         g_strip.pause_s, g_strip.current_segment + 1);
}

static void sequence_complete() {
    relay_request(false);

#if HAS_AUDIO
    audio_beep(AUDIO_SEQUENCE_DONE, 0);
#endif

    LOGI(TAG, "Sequence complete: %d segments, range %.1f-%.1fs",
         g_strip.segment_count,
         g_strip.segments[g_strip.segment_count - 1].cumulative_s,
         g_strip.segments[0].cumulative_s);

    // Snapshot strip data for deferred print log save (before state goes IDLE)
    {
        PrintLogStripData pd;
        pd.base_time_s   = g_strip.base_time_s;
        pd.step_label    = current_step_label();
        pd.step_stops    = current_step_value();
        pd.segment_count = g_strip.segment_count;
        for (int i = 0; i < g_strip.segment_count && i < PRINT_LOG_MAX_SEGMENTS; i++) {
            pd.segments[i].cumulative_s  = g_strip.segments[i].cumulative_s;
            pd.segments[i].incremental_s = g_strip.segments[i].incremental_s;
            pd.segments[i].offset_stops  = g_strip.segments[i].offset_stops;
        }
        print_log_pend_strip(pd);
    }

    g_strip.phase = STRIP_IDLE;
    g_strip.current_segment = 0;
}

static void cancel_sequence() {
    if (g_strip.phase == STRIP_IDLE) return;

    StripPhase prev = g_strip.phase;
    g_strip.phase = STRIP_IDLE;
    g_strip.current_segment = 0;
    if (relay_is_on()) relay_request(false);

    LOGI(TAG, "Cancelled from phase %d", prev);
}

// ============================================================================
// Commands
// ============================================================================

static void cmd_set_base(float t) {
    t = strip_snap_tenth(t);
    if (t < STRIP_BASE_TIME_MIN) t = STRIP_BASE_TIME_MIN;
    if (t > STRIP_BASE_TIME_MAX) t = STRIP_BASE_TIME_MAX;
    g_strip.base_time_s = t;
    recalculate_segments();
    LOGI(TAG, "Base time: %.1fs", t);
}

static void cmd_adjust_base(float delta) {
    cmd_set_base(g_strip.base_time_s + delta);
}

static void cmd_step_up() {
    if (g_strip.step_idx < STEP_TABLE_SIZE - 1) {
        g_strip.step_idx++;
        recalculate_segments();
        LOGI(TAG, "Step up: %s", current_step_label());
    }
}

static void cmd_step_down() {
    if (g_strip.step_idx > 0) {
        g_strip.step_idx--;
        recalculate_segments();
        LOGI(TAG, "Step down: %s", current_step_label());
    }
}

// Round to nearest odd number (3, 5, 7, 9, 11)
static int clamp_odd_segments(int n) {
    if (n < STRIP_SEGMENTS_MIN) n = STRIP_SEGMENTS_MIN;
    if (n > STRIP_SEGMENTS_MAX) n = STRIP_SEGMENTS_MAX;
    if (n % 2 == 0) n++;                    // round up to odd
    if (n > STRIP_SEGMENTS_MAX) n -= 2;     // back down if overshot
    return n;
}

static void cmd_adjust_segments(int delta) {
    // Step by 2 to stay on odd numbers
    if (delta > 0 && delta < 2) delta = 2;
    if (delta < 0 && delta > -2) delta = -2;
    int n = clamp_odd_segments(g_strip.segment_count + delta);
    g_strip.segment_count = n;
    recalculate_segments();
    LOGI(TAG, "Segments: %d (%+d)", n, delta);
}

static void cmd_set_segments(int n) {
    g_strip.segment_count = clamp_odd_segments(n);
    recalculate_segments();
    LOGI(TAG, "Segments: %d", g_strip.segment_count);
}

static void cmd_set_countdown(int secs) {
    if (secs < STRIP_COUNTDOWN_MIN) secs = STRIP_COUNTDOWN_MIN;
    if (secs > STRIP_COUNTDOWN_MAX) secs = STRIP_COUNTDOWN_MAX;
    g_strip.countdown_s = secs;
    LOGI(TAG, "Countdown: %ds", secs);
}

static void cmd_adjust_countdown(int delta) {
    cmd_set_countdown(g_strip.countdown_s + delta);
}

static void cmd_set_pause(int secs) {
    if (secs < STRIP_PAUSE_MIN) secs = STRIP_PAUSE_MIN;
    if (secs > STRIP_PAUSE_MAX) secs = STRIP_PAUSE_MAX;
    g_strip.pause_s = secs;
    LOGI(TAG, "Pause: %ds", secs);
}

static void cmd_adjust_pause(int delta) {
    cmd_set_pause(g_strip.pause_s + delta);
}

static void cmd_set_tick(bool on) {
    g_strip.exposure_tick = on;
    LOGI(TAG, "Exposure tick: %s", on ? "on" : "off");
}

// ============================================================================
// Binding resolver
// ============================================================================

static bool strip_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        snprintf(out, out_len, "ERR:no_key");
        return false;
    }

    // Split key;format at first ';' (bracket-depth-aware)
    char key_buf[32];
    const char* format = NULL;
    {
        int depth = 0;
        for (const char* p = params; *p; ++p) {
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            else if (*p == ';' && depth == 0) {
                size_t klen = p - params;
                if (klen >= sizeof(key_buf)) klen = sizeof(key_buf) - 1;
                memcpy(key_buf, params, klen);
                key_buf[klen] = '\0';
                params = key_buf;
                format = p + 1;
                break;
            }
        }
    }

    // state
    if (strcmp(params, "state") == 0) {
        const char* st;
        switch (g_strip.phase) {
            case STRIP_INITIAL_COUNTDOWN: st = "countdown"; break;
            case STRIP_EXPOSING:          st = "exposing";   break;
            case STRIP_BETWEEN_SEGMENTS:  st = "pausing";    break;

            default:                      st = "idle";       break;
        }
        snprintf(out, out_len, "%s", st);
        return true;
    }

    // segment (current, 1-based)
    if (strcmp(params, "segment") == 0) {
        snprintf(out, out_len, "%d", g_strip.current_segment);
        return true;
    }

    // segments (total count)
    if (strcmp(params, "segments") == 0) {
        snprintf(out, out_len, "%d", g_strip.segment_count);
        return true;
    }

    // remaining (current phase)
    if (strcmp(params, "remaining") == 0) {
        uint32_t rem = phase_remaining_ms();
        if (format) {
            strip_format_ms(rem, format, out, out_len);
        } else {
            snprintf(out, out_len, "%.1f", rem / 1000.0f);
        }
        return true;
    }

    // elapsed (current phase)
    if (strcmp(params, "elapsed") == 0) {
        uint32_t el = phase_elapsed_ms();
        if (format) {
            strip_format_ms(el, format, out, out_len);
        } else {
            snprintf(out, out_len, "%.1f", el / 1000.0f);
        }
        return true;
    }

    // seg_time:N — cumulative time for segment N
    if (strncmp(params, "seg_time:", 9) == 0) {
        int n = atoi(params + 9);
        if (n >= 1 && n <= g_strip.segment_count) {
            snprintf(out, out_len, "%.1f", g_strip.segments[n - 1].cumulative_s);
            return true;
        }
        snprintf(out, out_len, "---");
        return true;
    }

    // seg_offset:N — f-stop offset for segment N
    if (strncmp(params, "seg_offset:", 11) == 0) {
        int n = atoi(params + 11);
        if (n >= 1 && n <= g_strip.segment_count) {
            snprintf(out, out_len, "%+.1f", g_strip.segments[n - 1].offset_stops);
            return true;
        }
        snprintf(out, out_len, "---");
        return true;
    }

    // seg_inc:N — incremental time for segment N
    if (strncmp(params, "seg_inc:", 8) == 0) {
        int n = atoi(params + 8);
        if (n >= 1 && n <= g_strip.segment_count) {
            snprintf(out, out_len, "%.1f", g_strip.segments[n - 1].incremental_s);
            return true;
        }
        snprintf(out, out_len, "---");
        return true;
    }

    // seg_inc (no colon) — incremental time for current segment
    if (strcmp(params, "seg_inc") == 0) {
        if (g_strip.current_segment >= 1 && g_strip.current_segment <= g_strip.segment_count) {
            snprintf(out, out_len, "%.1f", g_strip.segments[g_strip.current_segment - 1].incremental_s);
        } else {
            snprintf(out, out_len, "---");
        }
        return true;
    }

    // base_time
    if (strcmp(params, "base_time") == 0) {
        snprintf(out, out_len, "%.1f", g_strip.base_time_s);
        return true;
    }

    // step (formatted as fraction, e.g. "1/3")
    if (strcmp(params, "step") == 0) {
        snprintf(out, out_len, "%s", current_step_label());
        return true;
    }

    // relay
    if (strcmp(params, "relay") == 0) {
        snprintf(out, out_len, "%s", relay_is_on() ? "ON" : "OFF");
        return true;
    }

    // progress
    if (strcmp(params, "progress") == 0) {
        snprintf(out, out_len, "%d/%d", g_strip.current_segment, g_strip.segment_count);
        return true;
    }

    // range
    if (strcmp(params, "range") == 0) {
        if (g_strip.segment_count > 0) {
            snprintf(out, out_len, "%.1f-%.1f",
                     g_strip.segments[g_strip.segment_count - 1].cumulative_s,
                     g_strip.segments[0].cumulative_s);
        } else {
            snprintf(out, out_len, "---");
        }
        return true;
    }

    // total_time
    if (strcmp(params, "total_time") == 0) {
        float total = estimate_total_time_s();
        if (format) {
            strip_format_ms((uint32_t)(total * 1000.0f), format, out, out_len);
        } else {
            snprintf(out, out_len, "%.0f", total);
        }
        return true;
    }

    // countdown (initial countdown setting)
    if (strcmp(params, "countdown") == 0) {
        snprintf(out, out_len, "%d", g_strip.countdown_s);
        return true;
    }

    // pause (inter-segment pause setting)
    if (strcmp(params, "pause") == 0) {
        snprintf(out, out_len, "%d", g_strip.pause_s);
        return true;
    }

    // tick (exposure tick setting)
    if (strcmp(params, "tick") == 0) {
        snprintf(out, out_len, "%s", g_strip.exposure_tick ? "on" : "off");
        return true;
    }

    // table — JSON payload for the table widget (darkroom-safe red tones)
    if (strcmp(params, "table") == 0) {
        int n = g_strip.segment_count;
        // Build a compact JSON object with red-tinted colors for darkroom use.
        // header_text_color / row_text_color use dim red shades to avoid fogging.
        int pos = 0;
        pos += snprintf(out + pos, out_len - pos,
            "{\"header_text_color\":\"#802020\",\"row_text_color\":\"#ff6060\","
            "\"columns\":[{\"key\":\"seg\",\"header\":\"#\",\"width_pct\":15},"
            "{\"key\":\"dur\",\"header\":\"Duration\",\"width_pct\":40},"
            "{\"key\":\"tot\",\"header\":\"Total\",\"width_pct\":45}],\"rows\":[");
        for (int i = 0; i < n && pos < (int)out_len - 80; i++) {
            if (i > 0) pos += snprintf(out + pos, out_len - pos, ",");
            // Highlight the active segment during a sequence
            bool active = (g_strip.phase == STRIP_EXPOSING && i == g_strip.current_segment - 1) ||
                          (g_strip.phase == STRIP_BETWEEN_SEGMENTS && i == g_strip.current_segment);
            if (active) {
                pos += snprintf(out + pos, out_len - pos,
                    "{\"seg\":\"%d\",\"dur\":\"%.1fs\",\"tot\":\"%.1fs\",\"_bg\":\"#401010\"}",
                    i + 1, g_strip.segments[i].incremental_s, g_strip.segments[i].cumulative_s);
            } else {
                pos += snprintf(out + pos, out_len - pos,
                    "{\"seg\":\"%d\",\"dur\":\"%.1fs\",\"tot\":\"%.1fs\"}",
                    i + 1, g_strip.segments[i].incremental_s, g_strip.segments[i].cumulative_s);
            }
        }
        pos += snprintf(out + pos, out_len - pos, "]}");
        return true;
    }

    snprintf(out, out_len, "ERR:bad_key");
    return false;
}

static void strip_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Public API
// ============================================================================

void test_strip_dispatch(const char* command, const char* value) {
    if (!command || !command[0]) {
        LOGW(TAG, "Empty command");
        return;
    }

    if (strcmp(command, "start") == 0)  { start_sequence(); return; }
    if (strcmp(command, "cancel") == 0) { cancel_sequence(); return; }

    // Configuration commands (only allowed when idle)
    if (g_strip.phase != STRIP_IDLE) {
        LOGW(TAG, "Config rejected (sequence active): '%s'", command);
        return;
    }

    if (strcmp(command, "set_base") == 0) {
        cmd_set_base(strtof(value, NULL));
        return;
    }
    if (strcmp(command, "adjust_base") == 0) {
        cmd_adjust_base(strtof(value, NULL));
        return;
    }
    if (strcmp(command, "step_up") == 0) {
        cmd_step_up();
        return;
    }
    if (strcmp(command, "step_down") == 0) {
        cmd_step_down();
        return;
    }
    if (strcmp(command, "adjust_segments") == 0) {
        cmd_adjust_segments(atoi(value));
        return;
    }
    if (strcmp(command, "set_segments") == 0) {
        cmd_set_segments(atoi(value));
        return;
    }
    if (strcmp(command, "set_countdown") == 0) {
        cmd_set_countdown(atoi(value));
        return;
    }
    if (strcmp(command, "adjust_countdown") == 0) {
        cmd_adjust_countdown(atoi(value));
        return;
    }
    if (strcmp(command, "set_pause") == 0) {
        cmd_set_pause(atoi(value));
        return;
    }
    if (strcmp(command, "adjust_pause") == 0) {
        cmd_adjust_pause(atoi(value));
        return;
    }
    if (strcmp(command, "set_tick") == 0) {
        cmd_set_tick(strcmp(value, "on") == 0);
        return;
    }

    LOGW(TAG, "Unknown command: '%s'", command);
}

void test_strip_tick() {
    if (g_strip.phase == STRIP_IDLE) return;

    uint32_t now = millis();
    uint32_t elapsed = now - g_strip.phase_start_ms;

    switch (g_strip.phase) {

    case STRIP_INITIAL_COUNTDOWN: {
#if HAS_AUDIO
        uint32_t remaining = (elapsed < g_strip.phase_duration_ms)
                             ? g_strip.phase_duration_ms - elapsed : 0;
        // Per-second ticks during the early portion
        if (remaining > PRE_EXPOSE_BEEP_LEAD_MS) {
            uint32_t tick_sec = elapsed / 1000;
            uint32_t last_sec = (g_strip.last_tick_ms > 0)
                                ? (g_strip.last_tick_ms - g_strip.phase_start_ms) / 1000 : UINT32_MAX;
            if (tick_sec != last_sec) {
                audio_beep(AUDIO_TICK, 0);
                g_strip.last_tick_ms = now;
            }
        }
        // Pre-expose beep in the last 3 seconds
        if (!g_strip.pre_beep_fired && remaining <= PRE_EXPOSE_BEEP_LEAD_MS) {
            audio_beep(AUDIO_PRE_EXPOSE_BEEP, 0);
            g_strip.pre_beep_fired = true;
        }
#endif
        if (elapsed >= g_strip.phase_duration_ms) {
            start_exposing();
        }
        break;
    }

    case STRIP_EXPOSING: {
#if HAS_AUDIO
        // Per-second exposure tick
        if (g_strip.exposure_tick && g_strip.phase_duration_ms > 2000) {
            uint32_t tick_sec = elapsed / 1000;
            uint32_t last_sec = (g_strip.last_tick_ms > 0)
                                ? (g_strip.last_tick_ms - g_strip.phase_start_ms) / 1000 : UINT32_MAX;
            if (tick_sec != last_sec && tick_sec > 0) {
                audio_beep(AUDIO_EXPOSE_TICK, 0);
                g_strip.last_tick_ms = now;
            }
        }
#endif
        if (elapsed >= g_strip.phase_duration_ms) {
            // Segment complete
            if (g_strip.current_segment >= g_strip.segment_count) {
                // Last segment
                sequence_complete();
            } else {
                start_between_segments();
            }
        }
        break;
    }

    case STRIP_BETWEEN_SEGMENTS: {
#if HAS_AUDIO
        // Pre-expose beep in the last 3 seconds of the pause
        if (!g_strip.pre_beep_fired) {
            uint32_t remaining = (elapsed < g_strip.phase_duration_ms)
                                 ? g_strip.phase_duration_ms - elapsed : 0;
            if (remaining <= PRE_EXPOSE_BEEP_LEAD_MS) {
                audio_beep(AUDIO_PRE_EXPOSE_BEEP, 0);
                g_strip.pre_beep_fired = true;
            }
        }
#endif
        if (elapsed >= g_strip.phase_duration_ms) {
            start_exposing();
        }
        break;
    }

    default:
        break;
    }
}

void test_strip_init() {
    recalculate_segments();
    if (!binding_template_register("strip", strip_resolve, strip_collect)) {
        LOGE(TAG, "Failed to register strip binding scheme");
    } else {
        LOGI(TAG, "Strip binding scheme registered");
    }
}

#else // !IS_DARKROOM_TIMER

void test_strip_init() {}
void test_strip_dispatch(const char*, const char*) {}
void test_strip_tick() {}

#endif // IS_DARKROOM_TIMER

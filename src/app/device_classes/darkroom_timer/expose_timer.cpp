#include "expose_timer.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "binding_template.h"
#include "config_manager.h"
#include "log_manager.h"
#include "meter.h"
#include "print_log.h"
#include "relay_controller.h"
#include "web_portal_state.h"

#if HAS_AUDIO
#include "audio.h"
#endif

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TAG "Expose"

// ============================================================================
// Constants
// ============================================================================

static constexpr float EXPOSE_TIME_MIN = 0.0f;
static constexpr float EXPOSE_TIME_MAX = 999.0f;
static constexpr float DRY_DOWN_MIN    = 0.0f;
static constexpr float DRY_DOWN_MAX    = 15.0f;

// ============================================================================
// State
// ============================================================================

enum ExposeState : uint8_t {
    EXPOSE_STOPPED = 0,
    EXPOSE_RUNNING = 1,
    EXPOSE_PAUSED  = 2,
    EXPOSE_FOCUS   = 3,
};

static struct {
    float        exposure_time_s;   // the setting (seconds)
    float        dry_down_pct;      // 0.0–15.0 (percent)
    ExposeState  state;
    uint32_t     start_ms;          // millis() at start/resume
    uint32_t     accumulated_ms;    // accumulated before pause
    bool         expire_fired;      // avoid re-firing
} g_expose = {
    .exposure_time_s = 10.0f,
    .dry_down_pct    = 0.0f,
    .state           = EXPOSE_STOPPED,
    .start_ms        = 0,
    .accumulated_ms  = 0,
    .expire_fired    = false,
};

// ============================================================================
// Timer helpers
// ============================================================================

// Round to 1 decimal place to prevent float drift from compounding
// across repeated fractional-stop adjustments.
static float snap_tenth(float v) {
    return roundf(v * 10.0f) / 10.0f;
}

static uint32_t raw_elapsed_ms() {
    uint32_t total = g_expose.accumulated_ms;
    if (g_expose.state == EXPOSE_RUNNING) {
        total += millis() - g_expose.start_ms;
    }
    return total;
}

static float effective_time_s() {
    if (g_expose.dry_down_pct == 0.0f) return g_expose.exposure_time_s;  // fast-path: no rounding
    return snap_tenth(g_expose.exposure_time_s * (1.0f - g_expose.dry_down_pct / 100.0f));
}

static uint32_t exposure_time_ms() {
    return (uint32_t)(effective_time_s() * 1000.0f);
}

static uint32_t remaining_ms() {
    uint32_t target = exposure_time_ms();
    uint32_t elapsed = raw_elapsed_ms();
    return (elapsed >= target) ? 0 : (target - elapsed);
}

// ============================================================================
// Timer format helper (reuse timer_engine patterns)
// ============================================================================

static int format_ms(uint32_t ms, const char* fmt, char* out, size_t out_len) {
    if (!fmt || !fmt[0]) fmt = "mm:ss";

    uint32_t total_sec = ms / 1000;
    uint32_t frac_ms   = ms % 1000;
    uint32_t h = total_sec / 3600;
    uint32_t m = (total_sec % 3600) / 60;
    uint32_t sec = total_sec % 60;
    uint32_t deci = frac_ms / 100;

    if (strcmp(fmt, "hh:mm:ss") == 0) {
        return snprintf(out, out_len, "%u:%02u:%02u", h, m, sec);
    } else if (strcmp(fmt, "ss") == 0) {
        return snprintf(out, out_len, "%u", (unsigned)(ms / 1000));
    } else if (strcmp(fmt, "mm:ss.d") == 0) {
        return snprintf(out, out_len, "%u:%02u.%u", m, sec, deci);
    } else if (strcmp(fmt, "ss.d") == 0) {
        return snprintf(out, out_len, "%u.%u", (unsigned)total_sec, deci);
    } else {
        // default mm:ss
        return snprintf(out, out_len, "%u:%02u", m, sec);
    }
}

// ============================================================================
// Commands
// ============================================================================

static void cmd_start() {
    if (g_expose.state == EXPOSE_RUNNING) return;
    if (g_expose.state == EXPOSE_FOCUS) {
        // Focus → running: lamp already on, just start countdown
        g_expose.accumulated_ms = 0;
        g_expose.expire_fired = false;
        g_expose.start_ms = millis();
        g_expose.state = EXPOSE_RUNNING;
        print_log_clear_id();
        LOGI(TAG, "Focus→Running %.1fs (effective %.1fs)", g_expose.exposure_time_s, effective_time_s());
        return;
    }
    // stopped or paused → start fresh
    g_expose.accumulated_ms = 0;
    g_expose.expire_fired = false;
    g_expose.start_ms = millis();
    g_expose.state = EXPOSE_RUNNING;
    relay_request(true);
    print_log_clear_id();
    LOGI(TAG, "Start %.1fs (effective %.1fs)", g_expose.exposure_time_s, effective_time_s());
}

static void cmd_stop() {
    if (g_expose.state == EXPOSE_STOPPED) return;
    g_expose.state = EXPOSE_STOPPED;
    g_expose.accumulated_ms = 0;
    g_expose.expire_fired = false;
    relay_request(false);
    LOGI(TAG, "Stop");
}

static void cmd_pause() {
    if (g_expose.state != EXPOSE_RUNNING) return;
    g_expose.accumulated_ms += millis() - g_expose.start_ms;
    g_expose.state = EXPOSE_PAUSED;
    relay_request(false);
    LOGI(TAG, "Pause at %ums", g_expose.accumulated_ms);
}

static void cmd_resume() {
    if (g_expose.state != EXPOSE_PAUSED) return;
    g_expose.start_ms = millis();
    g_expose.state = EXPOSE_RUNNING;
    relay_request(true);
    LOGI(TAG, "Resume");
}

static void cmd_toggle() {
    switch (g_expose.state) {
        case EXPOSE_STOPPED: cmd_start();  break;
        case EXPOSE_RUNNING: cmd_pause();  break;
        case EXPOSE_PAUSED:  cmd_resume(); break;
        case EXPOSE_FOCUS:   cmd_start();  break;
    }
}

static void cmd_reset() {
    g_expose.state = EXPOSE_STOPPED;
    g_expose.accumulated_ms = 0;
    g_expose.expire_fired = false;
    if (relay_is_on()) relay_request(false);
    LOGI(TAG, "Reset");
}

static void cmd_focus() {
    if (g_expose.state == EXPOSE_RUNNING || g_expose.state == EXPOSE_FOCUS) return;
    g_expose.state = EXPOSE_FOCUS;
    g_expose.accumulated_ms = 0;
    g_expose.expire_fired = false;
    relay_request(true);
    LOGI(TAG, "Focus ON");
}

static void cmd_focus_off() {
    if (g_expose.state != EXPOSE_FOCUS) return;
    g_expose.state = EXPOSE_STOPPED;
    relay_request(false);
    LOGI(TAG, "Focus OFF");
}

static void cmd_focus_toggle() {
    if (g_expose.state == EXPOSE_FOCUS) {
        cmd_focus_off();
    } else if (g_expose.state == EXPOSE_STOPPED || g_expose.state == EXPOSE_PAUSED) {
        cmd_focus();
    }
    // Running → no-op (don't interrupt an active exposure)
}

static void cmd_set_time(float t) {
    t = snap_tenth(t);
    if (t < EXPOSE_TIME_MIN) t = EXPOSE_TIME_MIN;
    if (t > EXPOSE_TIME_MAX) t = EXPOSE_TIME_MAX;
    g_expose.exposure_time_s = t;
    LOGI(TAG, "Set time %.1fs", t);
}

static void cmd_adjust_seconds(float delta) {
    cmd_set_time(snap_tenth(g_expose.exposure_time_s + delta));
    LOGI(TAG, "Adjust %.1fs → %.1fs", delta, g_expose.exposure_time_s);
}

static void cmd_adjust_stops(float stops) {
    cmd_set_time(snap_tenth(g_expose.exposure_time_s * powf(2.0f, stops)));
    LOGI(TAG, "Adjust %.3f stops → %.1fs", stops, g_expose.exposure_time_s);
}

static void cmd_set_dry_down(float pct) {
    if (g_expose.state == EXPOSE_RUNNING || g_expose.state == EXPOSE_PAUSED) {
        LOGW(TAG, "Dry-down change rejected (timer active)");
        return;
    }
    if (pct < DRY_DOWN_MIN) pct = DRY_DOWN_MIN;
    if (pct > DRY_DOWN_MAX) pct = DRY_DOWN_MAX;
    g_expose.dry_down_pct = snap_tenth(pct);
    LOGI(TAG, "Dry-down %.1f%% (effective %.1fs)", g_expose.dry_down_pct, effective_time_s());
}

static void cmd_adjust_dry_down(float delta) {
    cmd_set_dry_down(g_expose.dry_down_pct + delta);
}

// ============================================================================
// Binding resolver
// ============================================================================

static bool expose_resolve(const char* params, char* out, size_t out_len) {
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

    // Key: time (always the user's set time, unaffected by dry-down)
    if (strcmp(params, "time") == 0) {
        if (format) {
            format_ms((uint32_t)(g_expose.exposure_time_s * 1000.0f), format, out, out_len);
        } else {
            snprintf(out, out_len, "%.1f", g_expose.exposure_time_s);
        }
        return true;
    }

    // Key: remaining
    if (strcmp(params, "remaining") == 0) {
        uint32_t rem = (g_expose.state == EXPOSE_RUNNING || g_expose.state == EXPOSE_PAUSED)
                       ? remaining_ms() : exposure_time_ms();
        if (format) {
            format_ms(rem, format, out, out_len);
        } else {
            snprintf(out, out_len, "%.1f", rem / 1000.0f);
        }
        return true;
    }

    // Key: elapsed
    if (strcmp(params, "elapsed") == 0) {
        uint32_t el = (g_expose.state == EXPOSE_RUNNING || g_expose.state == EXPOSE_PAUSED)
                      ? raw_elapsed_ms() : 0;
        if (format) {
            format_ms(el, format, out, out_len);
        } else {
            snprintf(out, out_len, "%.1f", el / 1000.0f);
        }
        return true;
    }

    // Key: state
    if (strcmp(params, "state") == 0) {
        const char* st;
        switch (g_expose.state) {
            case EXPOSE_RUNNING: st = "running"; break;
            case EXPOSE_PAUSED:  st = "paused";  break;
            case EXPOSE_FOCUS:   st = "focus";    break;
            default:             st = "stopped";  break;
        }
        snprintf(out, out_len, "%s", st);
        return true;
    }

    // Key: relay
    if (strcmp(params, "relay") == 0) {
        snprintf(out, out_len, "%s", relay_is_on() ? "ON" : "OFF");
        return true;
    }

    // Key: dry_down
    if (strcmp(params, "dry_down") == 0) {
        snprintf(out, out_len, "%.1f", g_expose.dry_down_pct);
        return true;
    }

    // Key: effective_time
    if (strcmp(params, "effective_time") == 0) {
        float eff = effective_time_s();
        if (format) {
            format_ms((uint32_t)(eff * 1000.0f), format, out, out_len);
        } else {
            snprintf(out, out_len, "%.1f", eff);
        }
        return true;
    }

    snprintf(out, out_len, "ERR:bad_key");
    return false;
}

// No MQTT topics to collect
static void expose_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Public API
// ============================================================================

void expose_timer_dispatch(const char* command, const char* value) {
    if (!command || !command[0]) {
        LOGW(TAG, "Empty command");
        return;
    }

    if (strcmp(command, "start") == 0)      { cmd_start(); return; }
    if (strcmp(command, "stop") == 0)       { cmd_stop(); return; }
    if (strcmp(command, "toggle") == 0)     { cmd_toggle(); return; }
    if (strcmp(command, "pause") == 0)      { cmd_pause(); return; }
    if (strcmp(command, "resume") == 0)     { cmd_resume(); return; }
    if (strcmp(command, "reset") == 0)      { cmd_reset(); return; }
    if (strcmp(command, "focus") == 0)         { cmd_focus(); return; }
    if (strcmp(command, "focus_off") == 0)     { cmd_focus_off(); return; }
    if (strcmp(command, "focus_toggle") == 0)  { cmd_focus_toggle(); return; }

    if (strcmp(command, "set_time") == 0) {
        cmd_set_time(strtof(value, NULL));
        return;
    }
    if (strcmp(command, "adjust_seconds") == 0) {
        cmd_adjust_seconds(strtof(value, NULL));
        return;
    }
    if (strcmp(command, "adjust_stops") == 0) {
        cmd_adjust_stops(strtof(value, NULL));
        return;
    }
    if (strcmp(command, "set_dry_down") == 0) {
        cmd_set_dry_down(strtof(value, NULL));
        return;
    }
    if (strcmp(command, "adjust_dry_down") == 0) {
        cmd_adjust_dry_down(strtof(value, NULL));
        return;
    }

    LOGW(TAG, "Unknown command: '%s'", command);
}

void expose_timer_tick() {
    if (g_expose.state != EXPOSE_RUNNING) return;
    if (g_expose.expire_fired) return;

    if (raw_elapsed_ms() >= exposure_time_ms()) {
        g_expose.expire_fired = true;
        g_expose.state = EXPOSE_STOPPED;
        g_expose.accumulated_ms = 0;
        relay_request(false);
        LOGI(TAG, "Exposure complete");

        // Snapshot exposure + metering context for deferred print log save
        {
            PrintLogExposureData pd;
            pd.set_time_s      = g_expose.exposure_time_s;
            pd.effective_time_s = effective_time_s();
            pd.dry_down_pct    = g_expose.dry_down_pct;
            pd.lref            = meter_get_lref();
            pd.zone5_time      = meter_get_zone5_time();
            pd.l_bright        = meter_get_bright();
            pd.l_dark          = meter_get_dark();
            pd.sbr             = meter_get_sbr();
            pd.grade           = meter_get_grade();
            pd.grade_label     = meter_get_grade_label();
            pd.mag_factor      = meter_get_mag_factor();
            print_log_pend_exposure(pd);
        }

#if HAS_AUDIO
        // Play default tap beep as end-of-exposure alert
        DeviceConfig* cfg = web_portal_get_current_config();
        if (cfg && cfg->tap_beep[0]) {
            audio_beep(cfg->tap_beep, 0);
        } else {
            audio_beep("1000:300", 0);
        }
#endif
    }
}

float expose_timer_get_time() {
    return g_expose.exposure_time_s;  // atomic float read on ESP32
}

void expose_timer_set_time(float seconds) {
    cmd_set_time(seconds);  // reuses validation (clamp + snap_tenth)
}

void expose_timer_init() {
    if (!binding_template_register("expose", expose_resolve, expose_collect)) {
        LOGE(TAG, "Failed to register expose binding scheme");
    } else {
        LOGI(TAG, "Expose binding scheme registered");
    }
}

#else // !IS_DARKROOM_TIMER

void expose_timer_init() {}
void expose_timer_dispatch(const char*, const char*) {}
void expose_timer_tick() {}
float expose_timer_get_time() { return 0.0f; }
void expose_timer_set_time(float) {}

#endif // IS_DARKROOM_TIMER

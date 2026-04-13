#include "expose_timer.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "binding_template.h"
#include "config_manager.h"
#include "log_manager.h"
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
    ExposeState  state;
    uint32_t     start_ms;          // millis() at start/resume
    uint32_t     accumulated_ms;    // accumulated before pause
    bool         expire_fired;      // avoid re-firing
} s = {
    .exposure_time_s = 10.0f,
    .state           = EXPOSE_STOPPED,
    .start_ms        = 0,
    .accumulated_ms  = 0,
    .expire_fired    = false,
};

// ============================================================================
// Timer helpers
// ============================================================================

static uint32_t raw_elapsed_ms() {
    uint32_t total = s.accumulated_ms;
    if (s.state == EXPOSE_RUNNING) {
        total += millis() - s.start_ms;
    }
    return total;
}

static uint32_t exposure_time_ms() {
    return (uint32_t)(s.exposure_time_s * 1000.0f);
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
    if (s.state == EXPOSE_RUNNING) return;
    if (s.state == EXPOSE_FOCUS) {
        // Focus → running: lamp already on, just start countdown
        s.accumulated_ms = 0;
        s.expire_fired = false;
        s.start_ms = millis();
        s.state = EXPOSE_RUNNING;
        LOGI(TAG, "Focus→Running %.1fs", s.exposure_time_s);
        return;
    }
    // stopped or paused → start fresh
    s.accumulated_ms = 0;
    s.expire_fired = false;
    s.start_ms = millis();
    s.state = EXPOSE_RUNNING;
    relay_request(true);
    LOGI(TAG, "Start %.1fs", s.exposure_time_s);
}

static void cmd_stop() {
    if (s.state == EXPOSE_STOPPED) return;
    s.state = EXPOSE_STOPPED;
    s.accumulated_ms = 0;
    s.expire_fired = false;
    relay_request(false);
    LOGI(TAG, "Stop");
}

static void cmd_pause() {
    if (s.state != EXPOSE_RUNNING) return;
    s.accumulated_ms += millis() - s.start_ms;
    s.state = EXPOSE_PAUSED;
    relay_request(false);
    LOGI(TAG, "Pause at %ums", s.accumulated_ms);
}

static void cmd_resume() {
    if (s.state != EXPOSE_PAUSED) return;
    s.start_ms = millis();
    s.state = EXPOSE_RUNNING;
    relay_request(true);
    LOGI(TAG, "Resume");
}

static void cmd_toggle() {
    switch (s.state) {
        case EXPOSE_STOPPED: cmd_start();  break;
        case EXPOSE_RUNNING: cmd_pause();  break;
        case EXPOSE_PAUSED:  cmd_resume(); break;
        case EXPOSE_FOCUS:   cmd_start();  break;
    }
}

static void cmd_reset() {
    s.state = EXPOSE_STOPPED;
    s.accumulated_ms = 0;
    s.expire_fired = false;
    if (relay_is_on()) relay_request(false);
    LOGI(TAG, "Reset");
}

static void cmd_focus() {
    if (s.state == EXPOSE_RUNNING || s.state == EXPOSE_FOCUS) return;
    s.state = EXPOSE_FOCUS;
    s.accumulated_ms = 0;
    s.expire_fired = false;
    relay_request(true);
    LOGI(TAG, "Focus ON");
}

static void cmd_focus_off() {
    if (s.state != EXPOSE_FOCUS) return;
    s.state = EXPOSE_STOPPED;
    relay_request(false);
    LOGI(TAG, "Focus OFF");
}

static void cmd_focus_toggle() {
    if (s.state == EXPOSE_FOCUS) {
        cmd_focus_off();
    } else if (s.state == EXPOSE_STOPPED || s.state == EXPOSE_PAUSED) {
        cmd_focus();
    }
    // Running → no-op (don't interrupt an active exposure)
}

// Round to 1 decimal place to prevent float drift from compounding
// across repeated fractional-stop adjustments.
static float snap_tenth(float v) {
    return roundf(v * 10.0f) / 10.0f;
}

static void cmd_set_time(float t) {
    t = snap_tenth(t);
    if (t < EXPOSE_TIME_MIN) t = EXPOSE_TIME_MIN;
    if (t > EXPOSE_TIME_MAX) t = EXPOSE_TIME_MAX;
    s.exposure_time_s = t;
    LOGI(TAG, "Set time %.1fs", t);
}

static void cmd_add_seconds(float delta) {
    float t = snap_tenth(s.exposure_time_s + delta);
    if (t < EXPOSE_TIME_MIN) t = EXPOSE_TIME_MIN;
    if (t > EXPOSE_TIME_MAX) t = EXPOSE_TIME_MAX;
    s.exposure_time_s = t;
    LOGI(TAG, "Add %.1fs → %.1fs", delta, t);
}

static void cmd_add_stops(float stops) {
    float t = snap_tenth(s.exposure_time_s * powf(2.0f, stops));
    if (t < EXPOSE_TIME_MIN) t = EXPOSE_TIME_MIN;
    if (t > EXPOSE_TIME_MAX) t = EXPOSE_TIME_MAX;
    s.exposure_time_s = t;
    LOGI(TAG, "Add %.3f stops → %.1fs", stops, t);
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

    // Key: time
    if (strcmp(params, "time") == 0) {
        if (format) {
            format_ms(exposure_time_ms(), format, out, out_len);
        } else {
            // Show one decimal place
            snprintf(out, out_len, "%.1f", s.exposure_time_s);
        }
        return true;
    }

    // Key: remaining
    if (strcmp(params, "remaining") == 0) {
        uint32_t rem = (s.state == EXPOSE_RUNNING || s.state == EXPOSE_PAUSED)
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
        uint32_t el = (s.state == EXPOSE_RUNNING || s.state == EXPOSE_PAUSED)
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
        switch (s.state) {
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

void expose_timer_dispatch(const char* command) {
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

    if (strncmp(command, "set_time:", 9) == 0) {
        cmd_set_time(strtof(command + 9, NULL));
        return;
    }
    if (strncmp(command, "add_seconds:", 12) == 0) {
        cmd_add_seconds(strtof(command + 12, NULL));
        return;
    }
    if (strncmp(command, "add_stops:", 10) == 0) {
        cmd_add_stops(strtof(command + 10, NULL));
        return;
    }

    LOGW(TAG, "Unknown command: '%s'", command);
}

void expose_timer_tick() {
    if (s.state != EXPOSE_RUNNING) return;
    if (s.expire_fired) return;

    if (raw_elapsed_ms() >= exposure_time_ms()) {
        s.expire_fired = true;
        s.state = EXPOSE_STOPPED;
        s.accumulated_ms = 0;
        relay_request(false);
        LOGI(TAG, "Exposure complete");

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

void expose_timer_init() {
    if (!binding_template_register("expose", expose_resolve, expose_collect)) {
        LOGE(TAG, "Failed to register expose binding scheme");
    } else {
        LOGI(TAG, "Expose binding scheme registered");
    }
}

#else // !IS_DARKROOM_TIMER

void expose_timer_init() {}
void expose_timer_dispatch(const char*) {}
void expose_timer_tick() {}

#endif // IS_DARKROOM_TIMER

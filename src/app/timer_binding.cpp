#include "timer_binding.h"
#include "board_config.h"

#if HAS_DISPLAY

#include "binding_template.h"
#include "timer_engine.h"
#include "log_manager.h"

#include <string.h>
#include <stdio.h>

#define TAG "TimerBind"

// ============================================================================
// Helpers
// ============================================================================

// Parse "N" or "N_suffix" from params, returning timer id (1-based) or 0 on error.
// Sets *suffix to point after '_' if present, else NULL.
static uint8_t parse_timer_params(const char* params, const char** suffix) {
    *suffix = NULL;
    if (!params || !params[0]) return 0;

    uint8_t id = params[0] - '0';
    if (id < 1 || id > TIMER_COUNT) return 0;

    if (params[1] == '\0' || params[1] == ';') {
        return id;
    }
    if (params[1] == '_') {
        *suffix = params + 2;
        return id;
    }
    return 0;
}

// ============================================================================
// Resolver
// ============================================================================

static bool timer_binding_resolve(const char* params, char* out, size_t out_len) {
    // Split at first ';' for format override (only relevant for value keys)
    char buf[64];
    const char* format = NULL;
    if (params) {
        const char* semi = NULL;
        // Find ';' at bracket depth 0
        int depth = 0;
        for (const char* p = params; *p; ++p) {
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            else if (*p == ';' && depth == 0) { semi = p; break; }
        }
        if (semi) {
            size_t key_len = semi - params;
            if (key_len >= sizeof(buf)) key_len = sizeof(buf) - 1;
            memcpy(buf, params, key_len);
            buf[key_len] = '\0';
            params = buf;
            format = semi + 1;
        }
    }

    const char* suffix = NULL;
    uint8_t id = parse_timer_params(params, &suffix);
    if (id == 0) {
        snprintf(out, out_len, "ERR:bad_timer");
        return false;
    }

    // Meta keys: state, expired, mode, target
    if (suffix) {
        if (strcmp(suffix, "state") == 0) {
            TimerState st = timer_get_state(id);
            const char* s = (st == TIMER_RUNNING) ? "running" :
                            (st == TIMER_PAUSED)  ? "paused"  : "stopped";
            snprintf(out, out_len, "%s", s);
            return true;
        }
        if (strcmp(suffix, "expired") == 0) {
            snprintf(out, out_len, "%s", timer_is_expired(id) ? "ON" : "OFF");
            return true;
        }
        if (strcmp(suffix, "mode") == 0) {
            snprintf(out, out_len, "%s", timer_get_mode(id) == TIMER_MODE_DOWN ? "down" : "up");
            return true;
        }
        if (strcmp(suffix, "target") == 0) {
            snprintf(out, out_len, "%lu",
                     (unsigned long)timer_get_target_seconds(id));
            return true;
        }
        snprintf(out, out_len, "ERR:bad_key");
        return false;
    }

    // Value key: format timer
    timer_format(id, format, out, out_len);
    return true;
}

// ============================================================================
// Collector (no MQTT topics to collect)
// ============================================================================

static void timer_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
    // Timer bindings are local — no external subscriptions needed.
}

// ============================================================================
// Init
// ============================================================================

#if HAS_MCP
#include <ArduinoJson.h>
static void timer_scheme_describe(void* out) {
    JsonObject& o = *static_cast<JsonObject*>(out);
    o["syntax"]  = "[timer:N], [timer:N;format], [timer:N_state], [timer:N_mode], [timer:N_expired], or [timer:N_target]";
    o["example"] = "[timer:1]";
    o["keys"]    = "N=value, N_state=running|paused|stopped, N_mode=up|down, N_expired=ON|OFF, N_target=active countdown preset in whole seconds";
    o["note"]    = "timers 1-3; N_target is 0 for count-up or unconfigured timers";
}

// Validate Timer binding IDs and the complete supported suffix set.
static const char* timer_scheme_validate(const char* params) {
    if (!params || !params[0]) return nullptr;
    const char* suffix = nullptr;
    uint8_t id = parse_timer_params(params, &suffix);
    if (id == 0) {
        return "timer id must be 1-3 (e.g. [timer:1] or [timer:1_target])";
    }
    if (!suffix || strcmp(suffix, "state") == 0
            || strcmp(suffix, "mode") == 0
            || strcmp(suffix, "expired") == 0
            || strcmp(suffix, "target") == 0) {
        return nullptr;
    }
    return "timer key must be value, state, mode, expired, or target";
}
#endif

void timer_binding_init() {
    timer_engine_init();
    if (!binding_template_register("timer", timer_binding_resolve, timer_binding_collect)) {
        LOGE(TAG, "Failed to register timer binding scheme");
    } else {
        LOGI(TAG, "Timer binding scheme registered");
    }
#if HAS_MCP
    binding_template_set_scheme_describe("timer", timer_scheme_describe);
    binding_template_set_scheme_validate("timer", timer_scheme_validate);
#endif
}

#else // !HAS_DISPLAY

void timer_binding_init() {}

#endif // HAS_DISPLAY

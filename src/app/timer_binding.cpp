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

struct TimerBindingKeyDef {
    const char* name;
    uint8_t id;
    const char* suffix;
};

static const TimerBindingKeyDef kTimerBindingKeys[] = {
    {"1", 1, nullptr}, {"1_state", 1, "state"}, {"1_mode", 1, "mode"},
    {"1_expired", 1, "expired"}, {"1_target", 1, "target"},
    {"2", 2, nullptr}, {"2_state", 2, "state"}, {"2_mode", 2, "mode"},
    {"2_expired", 2, "expired"}, {"2_target", 2, "target"},
    {"3", 3, nullptr}, {"3_state", 3, "state"}, {"3_mode", 3, "mode"},
    {"3_expired", 3, "expired"}, {"3_target", 3, "target"},
};

static const TimerBindingKeyDef* find_timer_binding_key(const char* params) {
    if (!params) return nullptr;
    for (const TimerBindingKeyDef& key : kTimerBindingKeys) {
        if (strcmp(params, key.name) == 0) return &key;
    }
    return nullptr;
}

// ============================================================================
// Resolver
// ============================================================================

static BindingResolverStatus timer_binding_resolve(const char* params, char* out, size_t out_len) {
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

    const TimerBindingKeyDef* key = find_timer_binding_key(params);
    if (!key) {
        snprintf(out, out_len, "ERR:bad_timer");
        return BINDING_RESOLVER_UNKNOWN;
    }

    // Meta keys: state, expired, mode, target
    if (key->suffix) {
        if (strcmp(key->suffix, "state") == 0) {
            TimerState st = timer_get_state(key->id);
            const char* s = (st == TIMER_RUNNING) ? "running" :
                            (st == TIMER_PAUSED)  ? "paused"  : "stopped";
            snprintf(out, out_len, "%s", s);
            return BINDING_RESOLVER_RESOLVED;
        }
        if (strcmp(key->suffix, "expired") == 0) {
            snprintf(out, out_len, "%s", timer_is_expired(key->id) ? "ON" : "OFF");
            return BINDING_RESOLVER_RESOLVED;
        }
        if (strcmp(key->suffix, "mode") == 0) {
            snprintf(out, out_len, "%s", timer_get_mode(key->id) == TIMER_MODE_DOWN ? "down" : "up");
            return BINDING_RESOLVER_RESOLVED;
        }
        if (strcmp(key->suffix, "target") == 0) {
            snprintf(out, out_len, "%lu",
                     (unsigned long)timer_get_target_seconds(key->id));
            return BINDING_RESOLVER_RESOLVED;
        }
    }

    // Value key: format timer
    timer_format(key->id, format, out, out_len);
    return BINDING_RESOLVER_RESOLVED;
}

// ============================================================================
// Collector (no MQTT topics to collect)
// ============================================================================

static void timer_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
    // Timer bindings are local — no external subscriptions needed.
}

static uint8_t timer_binding_key_count() {
    return sizeof(kTimerBindingKeys) / sizeof(kTimerBindingKeys[0]);
}

static const char* timer_binding_key_at(uint8_t index) {
    return index < timer_binding_key_count() ? kTimerBindingKeys[index].name : nullptr;
}

// ============================================================================
// Init
// ============================================================================

void timer_binding_init() {
    timer_engine_init();
    if (!binding_template_register("timer", timer_binding_resolve, timer_binding_collect,
                                   {1, 2, 1, 1, BINDING_VALIDATION_STANDARD, false,
                                    timer_binding_key_count, timer_binding_key_at})) {
        LOGE(TAG, "Failed to register timer binding scheme");
    } else {
        LOGI(TAG, "Timer binding scheme registered");
    }
}

#else // !HAS_DISPLAY

void timer_binding_init() {}

#endif // HAS_DISPLAY

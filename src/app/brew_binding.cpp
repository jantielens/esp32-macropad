#include "brew_binding.h"
#include "board_config.h"

#if HAS_DISPLAY && HAS_SENSOR_HX711

#include "binding_template.h"
#include "brew_manager.h"
#include "log_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "BrewBind"

// ============================================================================
// Parse params: "key" or "key;format"
// ============================================================================

static void parse_brew_params(const char* params,
                              char* key, size_t key_len,
                              char* fmt, size_t fmt_len) {
    key[0] = '\0';
    fmt[0] = '\0';
    if (!params || !params[0]) return;

    const char* sep = strchr(params, ';');
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
// Key→value lookup
// ============================================================================

static bool lookup_value(const char* key, const char* fmt,
                         char* out, size_t out_len) {
    if (strcmp(key, "weight") == 0) {
        const char* f = fmt[0] ? fmt : "%.1f";
        snprintf(out, out_len, f, brew_get_weight());
        return true;
    }
    if (strcmp(key, "flow_rate") == 0) {
        const char* f = fmt[0] ? fmt : "%.1f";
        snprintf(out, out_len, f, brew_get_flow_rate());
        return true;
    }
    if (strcmp(key, "timer") == 0) {
        brew_format_timer(fmt, out, out_len);
        return true;
    }
    if (strcmp(key, "phase") == 0) {
        strlcpy(out, brew_get_phase_name(), out_len);
        return true;
    }
    if (strcmp(key, "active") == 0) {
        strlcpy(out, brew_is_active() ? "1" : "0", out_len);
        return true;
    }
    return false;
}

// ============================================================================
// Scheme resolver
// ============================================================================

static bool brew_binding_resolve(const char* params, char* out, size_t out_len) {
    char key[32];
    char fmt[32];
    parse_brew_params(params, key, sizeof(key), fmt, sizeof(fmt));

    if (!key[0]) {
        strlcpy(out, "ERR:no key", out_len);
        return false;
    }

    if (!lookup_value(key, fmt, out, out_len)) {
        strlcpy(out, "ERR:bad key", out_len);
        return false;
    }
    return true;
}

// ============================================================================
// No-op topic collector — brew data is local, no subscriptions needed
// ============================================================================

static void brew_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Init
// ============================================================================

void brew_binding_init() {
    if (!binding_template_register("brew", brew_binding_resolve, brew_binding_collect)) {
        LOGE(TAG, "Failed to register brew binding scheme");
    }
}

#else // !HAS_DISPLAY || !HAS_SENSOR_HX711

void brew_binding_init() {}

#endif

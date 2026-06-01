#include "scale_binding.h"
#include "board_config.h"

#if HAS_DISPLAY && HAS_SCALE

#include "binding_template.h"
#include "scale_hal.h"
#include "log_manager.h"

#include <string.h>
#include <stdio.h>

#define TAG "ScaleBind"

// ============================================================================
// Parse params: "key" or "key;format"
// ============================================================================

static void parse_scale_params(const char* params,
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

static bool lookup_value(const char* key, char* out, size_t out_len) {
    if (strcmp(key, "weight") == 0) {
        snprintf(out, out_len, "%.1f", scale_get_weight());
        return true;
    }
    if (strcmp(key, "flow_rate") == 0) {
        snprintf(out, out_len, "%.1f", scale_get_flow_rate());
        return true;
    }
    if (strcmp(key, "calibration_factor") == 0) {
        snprintf(out, out_len, "%.4f", scale_get_calibration_factor());
        return true;
    }
    if (strcmp(key, "offset") == 0) {
        snprintf(out, out_len, "%ld", scale_get_offset());
        return true;
    }
    if (strcmp(key, "available") == 0) {
        strlcpy(out, scale_is_available() ? "ON" : "OFF", out_len);
        return true;
    }
    if (strcmp(key, "cal_weight") == 0) {
        snprintf(out, out_len, "%.1f", scale_get_cal_weight());
        return true;
    }
    if (strcmp(key, "status") == 0) {
        strlcpy(out, scale_get_status(), out_len);
        return true;
    }
    return false;
}

// ============================================================================
// Scheme resolver — called by binding_template_resolve()
// ============================================================================

static bool scale_binding_resolve(const char* params, char* out, size_t out_len) {
    char key[32];
    char fmt[32];
    parse_scale_params(params, key, sizeof(key), fmt, sizeof(fmt));

    if (!key[0]) {
        strlcpy(out, "ERR:no key", out_len);
        return false;
    }

    char raw[64];
    if (!lookup_value(key, raw, sizeof(raw))) {
        strlcpy(out, "ERR:bad key", out_len);
        return false;
    }

    if (fmt[0]) {
        // Try float first (weight/flow are floats), fall back to string
        char* end = nullptr;
        double dval = strtod(raw, &end);
        if (end && *end == '\0') {
            snprintf(out, out_len, fmt, dval);
        } else {
            snprintf(out, out_len, fmt, raw);
        }
    } else {
        strlcpy(out, raw, out_len);
    }
    return true;
}

// ============================================================================
// No-op topic collector — scale data is local, no subscriptions needed
// ============================================================================

static void scale_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Init — register the "scale" scheme
// ============================================================================

void scale_binding_init() {
    if (!binding_template_register("scale", scale_binding_resolve, scale_binding_collect)) {
        LOGE(TAG, "Failed to register scale binding scheme");
    }
}

#else // !HAS_DISPLAY || !HAS_SCALE

void scale_binding_init() {}

#endif

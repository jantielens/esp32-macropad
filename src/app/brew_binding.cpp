#include "brew_binding.h"
#include "board_config.h"

#if HAS_DISPLAY && HAS_SCALE

#include "binding_template.h"
#include "brew_manager.h"
#include "brew_templates.h"
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
// Format helpers — reduce repetition in lookup_value()
// ============================================================================

static bool format_float(const char* fmt, const char* default_fmt,
                         float value, char* out, size_t out_len) {
    snprintf(out, out_len, fmt[0] ? fmt : default_fmt, value);
    return true;
}

static bool format_uint(const char* fmt, const char* default_fmt,
                        unsigned value, char* out, size_t out_len) {
    snprintf(out, out_len, fmt[0] ? fmt : default_fmt, value);
    return true;
}

// ============================================================================
// Key→value lookup
// ============================================================================

static bool lookup_value(const char* key, const char* fmt,
                         char* out, size_t out_len) {
    if (strcmp(key, "weight") == 0)
        return format_float(fmt, "%.1f", brew_get_weight(), out, out_len);
    if (strcmp(key, "flow_rate") == 0)
        return format_float(fmt, "%.1f", brew_get_flow_rate(), out, out_len);
    if (strcmp(key, "timer") == 0) {
        brew_format_timer(fmt, out, out_len);
        return true;
    }
    if (strcmp(key, "stage") == 0) {
        strlcpy(out, brew_get_stage_name(), out_len);
        return true;
    }
    if (strcmp(key, "active") == 0) {
        strlcpy(out, brew_is_active() ? "1" : "0", out_len);
        return true;
    }
    if (strcmp(key, "template") == 0) {
        const char* name = brew_get_template_name();
        strlcpy(out, name[0] ? name : "Idle", out_len);
        return true;
    }
    if (strcmp(key, "dose") == 0)
        return format_float(fmt, "%.1f", brew_get_dose_weight(), out, out_len);
    if (strcmp(key, "water") == 0)
        return format_float(fmt, "%.1f", brew_get_water_weight(), out, out_len);
    if (strcmp(key, "ratio") == 0) {
        float dose = brew_get_dose_weight();
        if (dose <= 0.0f) {
            strlcpy(out, "---", out_len);
            return true;
        }
        return format_float(fmt, "%.1f", brew_get_water_weight() / dose, out, out_len);
    }
    if (strcmp(key, "instruction") == 0) {
        const char* instr = brew_get_instruction();
        if (!instr[0]) return false;  // empty → binding returns fallback via pipe
        // Instruction text may contain inner bindings like [brew:stage_weight_target],
        // so resolve them before returning.
        binding_template_resolve(instr, out, out_len);
        return true;
    }
    if (strcmp(key, "next_label") == 0) {
        strlcpy(out, brew_get_next_label(), out_len);
        return true;
    }
    // ---- stage_weight_* ----
    if (strcmp(key, "stage_weight_target") == 0)
        return format_float(fmt, "%.0f", brew_get_stage_weight_target(), out, out_len);
    if (strcmp(key, "stage_weight_current") == 0)
        return format_float(fmt, "%.1f", brew_get_weight(), out, out_len);
    if (strcmp(key, "stage_weight_remaining") == 0)
        return format_float(fmt, "%.0f", brew_get_stage_weight_remaining(), out, out_len);
    if (strcmp(key, "stage_weight_pct") == 0) {
        float target = brew_get_stage_weight_target();
        float pct = (target > 0.0f) ? (brew_get_weight() / target * 100.0f) : 0.0f;
        return format_float(fmt, "%.0f", pct, out, out_len);
    }
    // ---- stage_time_* ----
    if (strcmp(key, "stage_time_target") == 0)
        return format_uint(fmt, "%u", (unsigned)(brew_get_stage_time_target_ms() / 1000), out, out_len);
    if (strcmp(key, "stage_time_current") == 0)
        return format_uint(fmt, "%u", (unsigned)(brew_get_stage_time_current_ms() / 1000), out, out_len);
    if (strcmp(key, "stage_time_remaining") == 0)
        return format_uint(fmt, "%u", (unsigned)(brew_get_stage_time_remaining_ms() / 1000), out, out_len);
    if (strcmp(key, "stage_time_pct") == 0) {
        uint32_t target_ms = brew_get_stage_time_target_ms();
        float pct = (target_ms > 0) ? ((float)brew_get_stage_time_current_ms() / (float)target_ms * 100.0f) : 0.0f;
        return format_float(fmt, "%.0f", pct, out, out_len);
    }
    // ---- stage_flow_* ----
    if (strcmp(key, "stage_flow_target") == 0)
        return format_float(fmt, "%.1f", brew_get_stage_flow_target(), out, out_len);
    if (strcmp(key, "stage_flow_current") == 0)
        return format_float(fmt, "%.1f", brew_get_flow_rate(), out, out_len);
    if (strcmp(key, "stage_flow_pct") == 0) {
        float target = brew_get_stage_flow_target();
        float pct = (target > 0.0f) ? (brew_get_flow_rate() / target * 100.0f) : 0.0f;
        return format_float(fmt, "%.0f", pct, out, out_len);
    }
    if (strcmp(key, "display_name") == 0) {
        const char* dn = brew_get_display_name();
        if (!dn[0]) return false;
        strlcpy(out, dn, out_len);
        return true;
    }
    // ---- template registry (indexed) ----
    if (strcmp(key, "template_count") == 0)
        return format_uint(fmt, "%u", brew_template_count(), out, out_len);
    if (strncmp(key, "tpl_", 4) == 0) {
        // Parse "tpl_N_field" — N is any unsigned index, capped by registry count
        const char* p = key + 4;
        char* end = nullptr;
        unsigned long idx = strtoul(p, &end, 10);
        if (end == p || *end != '_' || idx >= brew_template_count())
            return false;
        const char* field = end + 1;
        const BrewTemplate* t = brew_template_get((uint8_t)idx);
        if (!t) return false;
        if (strcmp(field, "name") == 0) {
            strlcpy(out, t->name, out_len);
            return true;
        }
        if (strcmp(field, "display_name") == 0) {
            strlcpy(out, t->display_name[0] ? t->display_name : t->name, out_len);
            return true;
        }
        if (strcmp(field, "description") == 0) {
            strlcpy(out, t->description, out_len);
            return true;
        }
        if (strcmp(field, "stages") == 0)
            return format_uint(fmt, "%u", t->stage_count, out, out_len);
        return false;  // unknown tpl_ field
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

#else // !HAS_DISPLAY || !HAS_SCALE

void brew_binding_init() {}

#endif

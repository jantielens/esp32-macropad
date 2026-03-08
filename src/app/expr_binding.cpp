#include "expr_binding.h"
#include "board_config.h"

#if HAS_DISPLAY

#include "binding_template.h"
#include "expr_eval.h"
#include "log_manager.h"

#include <string.h>
#include <stdio.h>

#define TAG "ExprBind"

// ============================================================================
// Format helper — reuse mqtt_sub_store_format_value if available
// ============================================================================

#if HAS_MQTT
extern "C" void mqtt_sub_store_format_value(const char* raw, const char* fmt, char* out, size_t out_len);
#endif

// Simple numeric format fallback (used when MQTT is not compiled in)
static void apply_format(const char* raw, const char* fmt, char* out, size_t out_len) {
#if HAS_MQTT
    mqtt_sub_store_format_value(raw, fmt, out, out_len);
#else
    // Basic format: try as double, fall back to string copy.
    // Validate that fmt is a simple printf float format (e.g. "%.1f", "%g")
    // to prevent format-string injection.
    bool fmt_ok = false;
    if (fmt[0] == '%') {
        const char* p = fmt + 1;
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '0' || *p == '#') p++;
        while (*p >= '0' && *p <= '9') p++;
        if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
        if ((*p == 'f' || *p == 'e' || *p == 'g' || *p == 'E' || *p == 'G') && *(p + 1) == '\0') fmt_ok = true;
    }
    char* end = NULL;
    double val = strtod(raw, &end);
    if (fmt_ok && end && end != raw && *end == '\0') {
        snprintf(out, out_len, fmt, val);
    } else {
        snprintf(out, out_len, "%s", raw);
    }
#endif
}

// ============================================================================
// Find format suffix — last ';' not inside quotes or brackets
// Returns pointer into buf at the format string (after ';'), or NULL.
// Null-terminates buf at the ';' position to split expression from format.
// Bracket depth tracking ensures ';' inside [mqtt:topic;path;fmt] is ignored.
// ============================================================================

static char* split_format(char* buf) {
    char* last_sep = NULL;
    bool in_quotes = false;
    int bracket_depth = 0;

    for (char* p = buf; *p; p++) {
        if (*p == '"') in_quotes = !in_quotes;
        else if (!in_quotes) {
            if (*p == '[') bracket_depth++;
            else if (*p == ']') { if (bracket_depth > 0) bracket_depth--; }
            else if (*p == ';' && bracket_depth == 0) last_sep = p;
        }
    }

    if (!last_sep) return NULL;

    *last_sep = '\0';
    char* fmt = last_sep + 1;
    while (*fmt == ' ' || *fmt == '\t') fmt++;
    return fmt[0] ? fmt : NULL;
}

// ============================================================================
// Detect unresolved placeholders ("---") in the expression
// ============================================================================

static bool has_placeholder(const char* s) {
    // Check for "---" which is the default when a binding hasn't received data
    return strstr(s, "---") != NULL;
}

// ============================================================================
// Scheme resolver — called by binding_template_resolve()
// ============================================================================

static bool expr_binding_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        snprintf(out, out_len, "ERR:empty expr");
        return false;
    }

    // Work buffer: copy params so we can modify (split format suffix)
    char buf[BINDING_TEMPLATE_MAX_LEN];
    snprintf(buf, sizeof(buf), "%s", params);

    // Split optional format suffix: "expression;%.1f"
    char* fmt = split_format(buf);

    // Step 1: Resolve inner bindings [mqtt:...], [health:...], [time:...] etc.
    char resolved[BINDING_TEMPLATE_MAX_LEN];
    binding_template_resolve(buf, resolved, sizeof(resolved));

    // Step 2: If any inner binding is still a placeholder, pass it through
    if (has_placeholder(resolved)) {
        snprintf(out, out_len, "---");
        return false;
    }

    // Step 3: Evaluate the expression
    char eval_result[EXPR_STR_MAX];
    if (!expr_eval(resolved, eval_result, sizeof(eval_result))) {
        // eval_result contains "ERR:xxx"
        snprintf(out, out_len, "%s", eval_result);
        return false;
    }

    // Step 4: Apply optional format
    if (fmt) {
        apply_format(eval_result, fmt, out, out_len);
    } else {
        snprintf(out, out_len, "%s", eval_result);
    }
    return true;
}

// ============================================================================
// Topic collector — forward inner binding tokens to collect_topics
// ============================================================================

static void expr_binding_collect(const char* params, void* user_data) {
    if (!params) return;

    // Strip format suffix (copy to avoid modifying original)
    char buf[BINDING_TEMPLATE_MAX_LEN];
    snprintf(buf, sizeof(buf), "%s", params);
    split_format(buf); // modifies buf in-place, we don't need the format here

    // Forward to binding_template_collect_topics — it will find inner [mqtt:...] etc.
    binding_template_collect_topics(buf, user_data);
}

// ============================================================================
// Init — register the "expr" scheme
// ============================================================================

void expr_binding_init(void) {
    if (!binding_template_register("expr", expr_binding_resolve, expr_binding_collect)) {
        LOGE(TAG, "Failed to register expr binding scheme");
    }
}

#else // !HAS_DISPLAY

void expr_binding_init(void) {}

#endif

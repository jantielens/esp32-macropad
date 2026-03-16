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
// Resolve inner binding tokens and auto-quote non-numeric results
// ============================================================================
// Walks the expression, resolves each [scheme:...] token individually.
// Numeric results are pasted as-is; text results are wrapped in "..."
// with embedded quotes and backslashes escaped.
// Returns true on success, false if any binding is unresolved (out = "---").

static bool resolve_and_quote(const char* expr, char* out, size_t out_len) {
    size_t wp = 0;
    const char* p = expr;
    bool in_quotes = false;

    while (*p && wp < out_len - 1) {
        if (*p == '\\' && in_quotes && *(p + 1)) {
            // Escape sequence inside user-written string — copy both chars
            if (wp + 2 > out_len - 1) break;
            out[wp++] = *p++;
            out[wp++] = *p++;
        } else if (*p == '"') {
            // Toggle user-written string mode and copy the quote
            in_quotes = !in_quotes;
            out[wp++] = *p++;
        } else if (*p == '[' && !in_quotes) {
            // Binding token — find matching ']' with bracket nesting
            int depth = 1;
            const char* tok_start = p;
            p++;
            while (*p && depth > 0) {
                if (*p == '[') depth++;
                else if (*p == ']') depth--;
                p++;
            }

            // Extract and resolve the token
            size_t tok_len = (size_t)(p - tok_start);
            char token[BINDING_TEMPLATE_MAX_LEN];
            if (tok_len >= sizeof(token)) tok_len = sizeof(token) - 1;
            memcpy(token, tok_start, tok_len);
            token[tok_len] = '\0';

            char resolved[BINDING_TEMPLATE_MAX_LEN];
            binding_template_resolve(token, resolved, sizeof(resolved));

            // Unresolved binding → whole expression is unresolved
            if (strcmp(resolved, "---") == 0) {
                snprintf(out, out_len, "---");
                return false;
            }

            // Check if the resolved value is numeric (consumed entirely by strtod)
            char* end = NULL;
            strtod(resolved, &end);
            bool is_numeric = (end && end != resolved && *end == '\0');

            if (is_numeric) {
                size_t rlen = strlen(resolved);
                if (wp + rlen >= out_len) break;
                memcpy(out + wp, resolved, rlen);
                wp += rlen;
            } else {
                // Wrap in quotes, escaping embedded " and backslash
                if (wp >= out_len - 1) break;
                out[wp++] = '"';
                for (const char* r = resolved; *r && wp < out_len - 2; r++) {
                    if (*r == '"' || *r == '\\') {
                        if (wp + 2 >= out_len - 1) break;
                        out[wp++] = '\\';
                    }
                    out[wp++] = *r;
                }
                if (wp >= out_len - 1) break;
                out[wp++] = '"';
            }
        } else {
            out[wp++] = *p++;
        }
    }
    out[wp] = '\0';
    return true;
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

    // Step 1: Resolve inner bindings, auto-quoting non-numeric text values
    char resolved[BINDING_TEMPLATE_MAX_LEN];
    if (!resolve_and_quote(buf, resolved, sizeof(resolved))) {
        snprintf(out, out_len, "---");
        return false;
    }

    // Step 2: Evaluate the expression
    char eval_result[EXPR_STR_MAX];
    if (!expr_eval(resolved, eval_result, sizeof(eval_result))) {
        snprintf(out, out_len, "%s", eval_result);
        return false;
    }

    // Step 3: Apply optional format
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

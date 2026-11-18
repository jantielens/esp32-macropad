#include "expr_binding.h"
#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "binding_template.h"
#include "binding_value.h"
#include "expr_eval.h"
#include "log_manager.h"
#include "time_service.h"

#include <cmath>
#include <limits>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "ExprBind"

static bool format_datetime(double value, const char* format, const char* timezone,
                            char* out, size_t out_len) {
    if (!std::isfinite(value) || std::trunc(value) != value ||
        std::fabs(value) > 9007199254740991.0 ||
        static_cast<long double>(value) < std::numeric_limits<time_t>::lowest() ||
        static_cast<long double>(value) > std::numeric_limits<time_t>::max() ||
        (timezone[0] && !time_service_timezone_valid(timezone))) return false;
    char expanded[128];
    size_t used = 0;
    for (const char* p = format; *p;) {
        const char* replacement = nullptr;
        size_t consumed = 1;
        if (*p == '%' && p[1] == '%') consumed = 2;
        else if (!strncmp(p, "%ums", 4)) return false;
        else if (!strncmp(p, "%ms", 3)) { replacement = "000"; consumed = 3; }
        else if (!strncmp(p, "%cs", 3)) { replacement = "00"; consumed = 3; }
        else if (!strncmp(p, "%ds", 3)) { replacement = "0"; consumed = 3; }
        const size_t count = replacement ? strlen(replacement) : consumed;
        if (count >= sizeof(expanded) - used) return false;
        memcpy(expanded + used, replacement ? replacement : p, count);
        used += count;
        p += consumed;
    }
    expanded[used] = '\0';
    return time_service_format(static_cast<time_t>(value), expanded, timezone, out, out_len);
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
        if (in_quotes && *p == '\\' && p[1]) p++;
        else if (*p == '"') in_quotes = !in_quotes;
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
    const bool checked = binding_template_checked_resolution_active();

    while (*p && wp < out_len - 1) {
        if (*p == '\\' && in_quotes && *(p + 1)) {
            // Escape sequence inside user-written string — copy both chars
            if (wp + 2 > out_len - 1) {
                if (checked) return false;
                break;
            }
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
            if (checked && (depth != 0 || tok_len >= sizeof(token))) return false;
            if (tok_len >= sizeof(token)) tok_len = sizeof(token) - 1;
            memcpy(token, tok_start, tok_len);
            token[tok_len] = '\0';

            char resolved[BINDING_TEMPLATE_MAX_LEN];
            const bool available = checked
                ? binding_template_resolve_checked(token, resolved, sizeof(resolved))
                : (binding_template_resolve(token, resolved, sizeof(resolved)), true);

            // Unresolved binding → whole expression is unresolved
            if (!available || (!checked && strcmp(resolved, "---") == 0)) {
                snprintf(out, out_len, "---");
                return false;
            }

            // Check if the resolved value is numeric (consumed entirely by strtod)
            char* end = NULL;
            strtod(resolved, &end);
            bool is_numeric = (end && end != resolved && *end == '\0') &&
                (!checked || binding_template_value_kind(token) != BINDING_VALUE_KIND_TEXT);

            if (is_numeric) {
                size_t rlen = strlen(resolved);
                if (wp + rlen >= out_len) {
                    if (checked) return false;
                    break;
                }
                memcpy(out + wp, resolved, rlen);
                wp += rlen;
            } else {
                // Wrap in quotes, escaping embedded " and backslash
                if (wp >= out_len - 1) {
                    if (checked) return false;
                    break;
                }
                out[wp++] = '"';
                const char* r = resolved;
                for (; *r && wp < out_len - 2; r++) {
                    if (*r == '"' || *r == '\\') {
                        if (wp + 2 >= out_len - 1) {
                            if (checked) return false;
                            break;
                        }
                        out[wp++] = '\\';
                    }
                    out[wp++] = *r;
                }
                if (checked && *r) return false;
                if (wp >= out_len - 1) {
                    if (checked) return false;
                    break;
                }
                out[wp++] = '"';
            }
        } else {
            out[wp++] = *p++;
        }
    }
    out[wp] = '\0';
    return !checked || !*p;
}

// ============================================================================
// Scheme resolver — called by binding_template_resolve()
// ============================================================================

static BindingResolverStatus expr_binding_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        snprintf(out, out_len, "ERR:empty expr");
        return BINDING_RESOLVER_UNAVAILABLE;
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
        return BINDING_RESOLVER_UNAVAILABLE;
    }

    // Step 2: Evaluate the expression
    char eval_result[EXPR_STR_MAX];
    const bool evaluated = binding_template_checked_resolution_active()
        ? expr_eval_checked_with_datetime(resolved, eval_result, sizeof(eval_result), format_datetime)
        : expr_eval_with_datetime(resolved, eval_result, sizeof(eval_result), format_datetime);
    if (!evaluated) {
        snprintf(out, out_len, "%s", eval_result);
        return BINDING_RESOLVER_UNAVAILABLE;
    }

    // Step 3: Apply optional format
    if (fmt) {
        binding_value_format(eval_result, fmt, out, out_len);
    } else {
        snprintf(out, out_len, "%s", eval_result);
    }
    return BINDING_RESOLVER_RESOLVED;
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

static const BindingParamDoc kExprParams[] = {
    {"expression", "Arithmetic, comparisons, ternaries, numeric functions, datetime(), duration(), and threshold(); inner tokens resolve first."},
    {"format", "printf format applied to the result, e.g. %.1f."},
};

static const BindingExampleDoc kExprExamples[] = {
    {"[expr:[health:cpu] > 80 ? \"High\" : \"OK\"]", "Text chosen by a condition."},
    {"[expr:([health:heap_free] / 1024);%.0f KB]", "Derived number with a unit."},
    {"[expr:abs([health:wifi_rssi])]", "Show the magnitude of a signed value."},
    {"[expr:clamp([health:cpu],0,100)]", "Keep a value within a range."},
    {"[expr:datetime(1700000000)]", "Format Unix seconds in the device timezone."},
    {"[expr:duration(65000,\"ms\")]", "Format milliseconds as minutes and seconds."},
    {"[expr:floor(4920000/60000);%.0f] minute(s)", "Show total minutes with a manual suffix."},
    {"[expr:threshold([health:cpu],\"#16a34a\",50,\"#f59e0b\",80,\"#dc2626\")]", "Map a number to color zones."},
    {"[expr:[time:%ms] % 500 < 250 ? \"#dc2626\" : \"#f8fafc\"]", "Blink between two colors."},
};

static const BindingReferenceDoc kExprReference[] = {
    {"Arithmetic", "a + b", nullptr, "Add; also - * /", "[expr:[health:heap_free] / 1024;%.0f]"},
    {"Arithmetic", "a % b", nullptr, "Remainder, useful for blinking", "[expr:[time:%ms] % 500 < 250]"},
    {"Arithmetic", "-a", nullptr, "Negate", "[expr:-[health:wifi_rssi]]"},
    {"Arithmetic", "( a )", nullptr, "Grouping", "[expr:([health:cpu] + 5) * 2]"},
    {"Comparison", "a > b", nullptr, "Also < >= <=; result is 1 or 0", "[expr:[health:cpu] > 80]"},
    {"Comparison", "a == b", nullptr, "Equal on numbers or \"strings\"; also !=", "[expr:[health:wifi_connected]==\"ON\"]"},
    {"Conditional", "c ? x : y", nullptr, "Ternary; branches can be numbers or \"strings\" and can be chained", "[expr:[health:cpu] > 80 ? \"High\" : \"OK\"]"},
    {"Functions", "abs(x)", nullptr, "Absolute value", "[expr:abs([health:wifi_rssi])]"},
    {"Functions", "min(a, b)", nullptr, "Smaller of two numbers", "[expr:min([health:cpu],80)]"},
    {"Functions", "max(a, b)", nullptr, "Larger of two numbers", "[expr:max([health:cpu],20)]"},
    {"Functions", "clamp(x, low, high)", nullptr, "Limit a number to an inclusive range; low must not exceed high", "[expr:clamp([health:cpu],0,100)]"},
    {"Functions", "round(x)", nullptr, "Nearest integer; halfway values round away from zero", "[expr:round([health:cpu])]"},
    {"Functions", "floor(x)", nullptr, "Round down to the next integer", "[expr:floor([health:cpu])]"},
    {"Functions", "ceil(x)", nullptr, "Round up to the next integer", "[expr:ceil([health:cpu])]"},
    {"Functions", "threshold(v, c0, t1, c1, ...)", nullptr, "c0 below t1, c1 below t2, ..., else the last value; thresholds ascend", "[expr:threshold([health:cpu],\"#16a34a\",50,\"#dc2626\")]"},
    {"Date & duration", "datetime(epoch, format, timezone)", "Device-local time",
     "Integer Unix seconds; format and timezone are optional. Defaults to device timezone and %Y-%m-%d %H:%M.",
     "[expr:datetime(1700000000)]"},
    {"Date & duration", "duration(value, unit, format)", "1:05",
     "Unit is s or ms; format defaults to mm:ss. Also supports hh:mm:ss, mm:ss.d, ss, and ss.d. Negative values are supported; precision is truncated.",
     "[expr:duration(65000,\"ms\")]"},
    {"Date & duration", "Manual suffix", "82 minute(s)",
     "Use arithmetic and a literal suffix for total minutes.",
     "[expr:floor(4920000/60000);%.0f] minute(s)"},
    {"Date & duration", "Minutes and seconds", "5 min and 3 sec",
     "Combine expressions and literal suffixes for custom units.",
     "[expr:floor(303000/60000);%.0f] min and [expr:floor(303000/1000)%60;%.0f] sec"},
    {"Date & duration", "Hours and minutes", "1 hour(s) and 5 minute(s)",
     "For nonnegative durations, divide into whole units and add literal suffixes.",
     "[expr:floor(3900000/3600000);%.0f] hour(s) and [expr:floor(3900000/60000)%60;%.0f] minute(s)"},
};

static const BindingSchemeDoc kExprDoc = {
    "Logic", "Calculate values, compare them, and choose text or colors.",
    BINDING_DOC_LIST(kExprParams), BINDING_DOC_NONE, BINDING_DOC_LIST(kExprExamples),
    "Operators & functions", false, BINDING_DOC_LIST(kExprReference),
    "Quote strings with double quotes. There are no && or || operators; nest ternaries instead.", nullptr,
};

void expr_binding_init(void) {
    if (!binding_template_register("expr", expr_binding_resolve, expr_binding_collect,
                                   {1, 2, 1, 1, BINDING_VALIDATION_EXPRESSION, true, nullptr, nullptr,
                                    &kExprDoc})) {
        LOGE(TAG, "Failed to register expr binding scheme");
    }
}

#else // !HAS_DISPLAY && !HAS_BUTTON

void expr_binding_init(void) {}

#endif

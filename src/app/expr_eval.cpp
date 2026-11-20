// ============================================================================
// Expression Evaluator — Pure C, no Arduino/ESP32 dependencies
// ============================================================================
// Recursive-descent parser for arithmetic, comparison, and ternary expressions.
// See expr_eval.h for supported syntax.

#include "expr_eval.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits>

// ============================================================================
// Value type — tagged union for numbers and strings
// ============================================================================

typedef enum { VAL_NUM, VAL_STR, VAL_ERR } ExprValType;

typedef struct {
    ExprValType type;
    double      num;
    const char* str;
    size_t      str_len;
} ExprVal;

static ExprVal make_num(double v) {
    ExprVal r;
    r.type = VAL_NUM;
    r.num  = v;
    r.str = NULL;
    r.str_len = 0;
    return r;
}

static ExprVal make_str(const char* s, size_t len) {
    ExprVal r;
    r.type = VAL_STR;
    r.num  = 0;
    if (len >= EXPR_STR_MAX) len = EXPR_STR_MAX - 1;
    r.str = s;
    r.str_len = len;
    return r;
}

static ExprVal make_err(const char* msg) {
    ExprVal r;
    r.type = VAL_ERR;
    r.num  = 0;
    r.str = msg;
    r.str_len = strlen(msg);
    return r;
}

static bool val_is_truthy(const ExprVal* v) {
    if (v->type == VAL_NUM) return v->num != 0.0;
    if (v->type == VAL_STR) return v->str_len != 0;
    return false;
}

// ============================================================================
// Parser state
// ============================================================================

typedef struct {
    const char* src;
    int         pos;
    uint8_t     nesting;
    char        strings[EXPR_STR_MAX * 4];
    size_t      strings_used;
    expr_datetime_format_fn datetime_format;
    bool        checked;
} Parser;

static void skip_ws(Parser* p) {
    while (p->src[p->pos] == ' ' || p->src[p->pos] == '\t' ||
           p->src[p->pos] == '\r' || p->src[p->pos] == '\n') {
        p->pos++;
    }
}

static bool match_char(Parser* p, char c) {
    skip_ws(p);
    if (p->src[p->pos] == c) {
        p->pos++;
        return true;
    }
    return false;
}

// ============================================================================
// Forward declarations
// ============================================================================

static ExprVal parse_ternary(Parser* p);

static ExprVal parse_nested_ternary(Parser* p) {
    if (p->nesting >= EXPR_MAX_NESTING) return make_err("depth");
    p->nesting++;
    ExprVal value = parse_ternary(p);
    p->nesting--;
    return value;
}

typedef double (*ExprUnaryMathFn)(double);
typedef double (*ExprBinaryMathFn)(double, double);

static ExprVal parse_unary_math_function(Parser* p, size_t name_len, ExprUnaryMathFn fn) {
    p->pos += (int)name_len + 1;
    ExprVal arg = parse_nested_ternary(p);
    if (arg.type == VAL_ERR) return arg;
    if (arg.type != VAL_NUM) return make_err("function: argument must be a number");
    if (!match_char(p, ')')) return make_err("function: expected one argument");
    return make_num(fn(arg.num));
}

static ExprVal parse_binary_math_function(Parser* p, size_t name_len, ExprBinaryMathFn fn) {
    p->pos += (int)name_len + 1;
    ExprVal left = parse_nested_ternary(p);
    if (left.type == VAL_ERR) return left;
    if (left.type != VAL_NUM) return make_err("function: arguments must be numbers");
    if (!match_char(p, ',')) return make_err("function: expected two arguments");

    ExprVal right = parse_nested_ternary(p);
    if (right.type == VAL_ERR) return right;
    if (right.type != VAL_NUM) return make_err("function: arguments must be numbers");
    if (!match_char(p, ')')) return make_err("function: expected two arguments");
    return make_num(fn(left.num, right.num));
}

static ExprVal parse_clamp_function(Parser* p) {
    p->pos += 6;
    ExprVal value = parse_nested_ternary(p);
    if (value.type == VAL_ERR) return value;
    if (value.type != VAL_NUM) return make_err("clamp: arguments must be numbers");
    if (!match_char(p, ',')) return make_err("clamp: expected three arguments");

    ExprVal low = parse_nested_ternary(p);
    if (low.type == VAL_ERR) return low;
    if (low.type != VAL_NUM) return make_err("clamp: arguments must be numbers");
    if (!match_char(p, ',')) return make_err("clamp: expected three arguments");

    ExprVal high = parse_nested_ternary(p);
    if (high.type == VAL_ERR) return high;
    if (high.type != VAL_NUM) return make_err("clamp: arguments must be numbers");
    if (!match_char(p, ')')) return make_err("clamp: expected three arguments");
    if (low.num > high.num) return make_err("clamp: lower bound exceeds upper bound");
    return make_num(fmax(low.num, fmin(value.num, high.num)));
}

static bool copy_arg_string(const ExprVal& value, char* out, size_t size) {
    if (value.type != VAL_STR || value.str_len >= size) return false;
    memcpy(out, value.str, value.str_len);
    out[value.str_len] = '\0';
    return true;
}

static bool format_duration(double value, const char* unit, const char* format,
                            char* out, size_t out_len) {
    if (!isfinite(value) || (strcmp(unit, "s") && strcmp(unit, "ms"))) return false;
    double tenths_value = fabs(value) * (strcmp(unit, "s") == 0 ? 10.0 : 0.01);
    if (!isfinite(tenths_value) || tenths_value >= 90071992547409.0) return false;
    uint64_t tenths = static_cast<uint64_t>(tenths_value);
    uint64_t seconds = tenths / 10;
    uint64_t minutes = seconds / 60;
    const char* sign = value < 0 ? "-" : "";
    int n = -1;
    if (!strcmp(format, "mm:ss"))
        n = snprintf(out, out_len, "%s%llu:%02u", sign, (unsigned long long)minutes, (unsigned)(seconds % 60));
    else if (!strcmp(format, "hh:mm:ss"))
        n = snprintf(out, out_len, "%s%llu:%02u:%02u", sign, (unsigned long long)(minutes / 60),
                     (unsigned)(minutes % 60), (unsigned)(seconds % 60));
    else if (!strcmp(format, "mm:ss.d"))
        n = snprintf(out, out_len, "%s%llu:%02u.%u", sign, (unsigned long long)minutes,
                     (unsigned)(seconds % 60), (unsigned)(tenths % 10));
    else if (!strcmp(format, "ss"))
        n = snprintf(out, out_len, "%s%llu", sign, (unsigned long long)seconds);
    else if (!strcmp(format, "ss.d"))
        n = snprintf(out, out_len, "%s%llu.%u", sign, (unsigned long long)seconds, (unsigned)(tenths % 10));
    return n >= 0 && static_cast<size_t>(n) < out_len;
}

static ExprVal parse_format_function(Parser* p, bool datetime) {
    p->pos += 9;
    ExprVal value = parse_nested_ternary(p);
    if (value.type == VAL_ERR) return value;
    if (value.type != VAL_NUM) return make_err("format: value must be numeric");
    char first[128] = "";
    char second[128] = "";
    if (match_char(p, ',')) {
        ExprVal arg = parse_nested_ternary(p);
        if (arg.type == VAL_ERR) return arg;
        if (!copy_arg_string(arg, first, sizeof(first))) return make_err("format: invalid argument");
        if (match_char(p, ',')) {
            arg = parse_nested_ternary(p);
            if (arg.type == VAL_ERR) return arg;
            if (!copy_arg_string(arg, second, sizeof(second))) return make_err("format: invalid argument");
        }
    } else if (!datetime) {
        return make_err("duration: unit required");
    }
    if (!match_char(p, ')')) return make_err("format: expected closing ')'");
    char result[EXPR_STR_MAX];
    bool ok = datetime
        ? p->datetime_format && p->datetime_format(value.num, first[0] ? first : "%Y-%m-%d %H:%M",
                                                    second, result, sizeof(result))
        : format_duration(value.num, first, second[0] ? second : "mm:ss", result, sizeof(result));
    if (!ok) return make_err("invalid date or duration");
    size_t len = strlen(result);
    if (len + 1 > sizeof(p->strings) - p->strings_used) return make_err("formatted text too long");
    char* stored = p->strings + p->strings_used;
    memcpy(stored, result, len + 1);
    p->strings_used += len + 1;
    return make_str(stored, len);
}

// ============================================================================
// Primary: number, string literal, parenthesized expression
// ============================================================================

static ExprVal parse_primary(Parser* p) {
    skip_ws(p);
    char c = p->src[p->pos];

    if (strncmp(p->src + p->pos, "datetime(", 9) == 0)
        return parse_format_function(p, true);
    if (strncmp(p->src + p->pos, "duration(", 9) == 0)
        return parse_format_function(p, false);
    // Built-in function: threshold(value, color0, t1, color1, ..., tN, colorN)
    // Stream-processed so variable-length calls use fixed stack space.
    if (strncmp(p->src + p->pos, "threshold(", 10) == 0) {
        p->pos += 10; // skip "threshold("

        // Empty arg list?
        skip_ws(p);
        if (p->src[p->pos] == ')') return make_err("threshold: need value+color");

        // Parse value (first arg, must be numeric)
        ExprVal value_arg = parse_nested_ternary(p);
        if (value_arg.type == VAL_ERR) return value_arg;
        if (value_arg.type != VAL_NUM) return make_err("threshold: value must be number");

        // Need at least value + base color
        if (!match_char(p, ','))
            return make_err("threshold: need value, color [, thresh, color]*");

        // Parse base color (second arg)
        ExprVal result = parse_nested_ternary(p);
        if (result.type == VAL_ERR) return result;
        if (result.type != VAL_STR) return make_err("threshold: color must be string");

        double value = value_arg.num;
        double prev_threshold = 0;
        bool   has_prev_thresh = false;
        bool   decided = false; // true once value < threshold found

        // Parse threshold-color pairs
        while (match_char(p, ',')) {
            ExprVal thresh = parse_nested_ternary(p);
            if (thresh.type == VAL_ERR) return thresh;
            if (thresh.type != VAL_NUM)
                return make_err("threshold: threshold must be number");
            if (has_prev_thresh && thresh.num <= prev_threshold)
                return make_err("threshold: thresholds must be ascending");
            prev_threshold  = thresh.num;
            has_prev_thresh = true;

            // Each threshold must be followed by a paired color
            if (!match_char(p, ','))
                return make_err("threshold: need value, color [, thresh, color]*");

            ExprVal color = parse_nested_ternary(p);
            if (color.type == VAL_ERR) return color;
            if (color.type != VAL_STR)
                return make_err("threshold: color must be string");

            if (!decided) {
                if (value < thresh.num) {
                    decided = true; // result already holds the correct color
                } else {
                    result = color;
                }
            }
        }

        if (!match_char(p, ')')) return make_err("threshold: missing ')'");
        return result;
    }

    if (strncmp(p->src + p->pos, "abs(", 4) == 0)
        return parse_unary_math_function(p, 3, fabs);
    if (strncmp(p->src + p->pos, "round(", 6) == 0)
        return parse_unary_math_function(p, 5, round);
    if (strncmp(p->src + p->pos, "floor(", 6) == 0)
        return parse_unary_math_function(p, 5, floor);
    if (strncmp(p->src + p->pos, "ceil(", 5) == 0)
        return parse_unary_math_function(p, 4, ceil);
    if (strncmp(p->src + p->pos, "min(", 4) == 0)
        return parse_binary_math_function(p, 3, fmin);
    if (strncmp(p->src + p->pos, "max(", 4) == 0)
        return parse_binary_math_function(p, 3, fmax);
    if (strncmp(p->src + p->pos, "clamp(", 6) == 0)
        return parse_clamp_function(p);

    // Parenthesized expression
    if (c == '(') {
        p->pos++;
        ExprVal v = parse_nested_ternary(p);
        if (v.type == VAL_ERR) return v;
        if (!match_char(p, ')')) return make_err("missing ')'");
        return v;
    }

    // String literal
    if (c == '"') {
        p->pos++;
        const char* start = p->src + p->pos;
        while (p->src[p->pos] && p->src[p->pos] != '"') {
            if (p->src[p->pos] == '\\' && p->src[p->pos + 1]) {
                p->pos++; // skip escaped char
            }
            p->pos++;
        }
        if (p->src[p->pos] != '"') return make_err("unclosed string");
        size_t len = (size_t)(p->src + p->pos - start);
        p->pos++; // skip closing "
        if (p->checked) {
            if (p->strings_used >= sizeof(p->strings)) return make_err("string too long");
            char* stored = p->strings + p->strings_used;
            size_t used = 0;
            for (size_t i = 0; i < len; ++i) {
                if (start[i] == '\\' && i + 1 < len &&
                    (start[i + 1] == '\\' || start[i + 1] == '"')) ++i;
                if (used >= EXPR_STR_MAX - 1 ||
                    used + 1 >= sizeof(p->strings) - p->strings_used)
                    return make_err("string too long");
                stored[used++] = start[i];
            }
            stored[used] = '\0';
            p->strings_used += used + 1;
            return make_str(stored, used);
        }
        return make_str(start, len);
    }

    // Number (including leading dot like .5)
    if ((c >= '0' && c <= '9') || c == '.') {
        char* end = NULL;
        double val = strtod(p->src + p->pos, &end);
        if (end == p->src + p->pos) return make_err("bad number");
        p->pos = (int)(end - p->src);
        return make_num(val);
    }

    if (c == '\0') return make_err("unexpected end");
    return make_err("unexpected char");
}

// ============================================================================
// Unary: -expr, +expr, or primary
// ============================================================================

static ExprVal parse_unary(Parser* p) {
    skip_ws(p);
    char op = p->src[p->pos];
    if (op == '-' || op == '+') {
        p->pos++;
        if (p->nesting >= EXPR_MAX_NESTING) return make_err("depth");
        p->nesting++;
        ExprVal v = parse_unary(p);
        p->nesting--;
        if (v.type == VAL_ERR) return v;
        if (v.type != VAL_NUM)
            return make_err(op == '-' ? "unary - on non-number" : "unary + on non-number");
        return op == '-' ? make_num(-v.num) : v;
    }
    return parse_primary(p);
}

// ============================================================================
// Multiplicative: * / %
// ============================================================================

static ExprVal parse_multiplicative(Parser* p) {
    ExprVal left = parse_unary(p);
    if (left.type == VAL_ERR) return left;

    while (true) {
        skip_ws(p);
        char op = p->src[p->pos];
        if (op != '*' && op != '/' && op != '%') break;
        p->pos++;

        ExprVal right = parse_unary(p);
        if (right.type == VAL_ERR) return right;
        if (left.type != VAL_NUM || right.type != VAL_NUM)
            return make_err("arithmetic on non-number");

        if (op == '*') left.num *= right.num;
        else if (op == '/') {
            if (right.num == 0.0) return make_err("div/0");
            left.num /= right.num;
        } else {
            if (right.num == 0.0) return make_err("mod/0");
            left.num = fmod(left.num, right.num);
        }
    }
    return left;
}

// ============================================================================
// Additive: + -
// ============================================================================

static ExprVal parse_additive(Parser* p) {
    ExprVal left = parse_multiplicative(p);
    if (left.type == VAL_ERR) return left;

    while (true) {
        skip_ws(p);
        char c = p->src[p->pos];
        if (c != '+' && c != '-') break;
        p->pos++;

        ExprVal right = parse_multiplicative(p);
        if (right.type == VAL_ERR) return right;
        if (left.type != VAL_NUM || right.type != VAL_NUM)
            return make_err("arithmetic on non-number");

        if (c == '+') left.num += right.num;
        else left.num -= right.num;
    }
    return left;
}

// ============================================================================
// Comparison: > < >= <= == !=
// ============================================================================

static ExprVal parse_comparison(Parser* p) {
    ExprVal left = parse_additive(p);
    if (left.type == VAL_ERR) return left;

    skip_ws(p);
    char c1 = p->src[p->pos];
    char c2 = p->src[p->pos + 1];

    int op = 0; // 0=none, 1=>, 2=<, 3=>=, 4=<=, 5===, 6=!=
    int advance = 0;

    if (c1 == '>' && c2 == '=')      { op = 3; advance = 2; }
    else if (c1 == '<' && c2 == '=')  { op = 4; advance = 2; }
    else if (c1 == '=' && c2 == '=')  { op = 5; advance = 2; }
    else if (c1 == '!' && c2 == '=')  { op = 6; advance = 2; }
    else if (c1 == '>')               { op = 1; advance = 1; }
    else if (c1 == '<')               { op = 2; advance = 1; }

    if (op == 0) return left;
    p->pos += advance;

    ExprVal right = parse_additive(p);
    if (right.type == VAL_ERR) return right;

    // String equality/inequality
    if (op == 5 || op == 6) {
        if (left.type == VAL_STR && right.type == VAL_STR) {
            bool equal = left.str_len == right.str_len &&
                         memcmp(left.str, right.str, left.str_len) == 0;
            return make_num(op == 5 ? (equal ? 1.0 : 0.0) : (equal ? 0.0 : 1.0));
        }
    }

    // Numeric comparison
    if (left.type != VAL_NUM || right.type != VAL_NUM)
        return make_err("comparison on non-number");

    double result = 0.0;
    switch (op) {
        case 1: result = left.num >  right.num ? 1.0 : 0.0; break;
        case 2: result = left.num <  right.num ? 1.0 : 0.0; break;
        case 3: result = left.num >= right.num ? 1.0 : 0.0; break;
        case 4: result = left.num <= right.num ? 1.0 : 0.0; break;
        case 5: result = left.num == right.num ? 1.0 : 0.0; break;
        case 6: result = left.num != right.num ? 1.0 : 0.0; break;
    }
    return make_num(result);
}

// ============================================================================
// Ternary: comparison ? value : value
// ============================================================================

static ExprVal parse_ternary(Parser* p) {
    ExprVal cond = parse_comparison(p);
    if (cond.type == VAL_ERR) return cond;

    if (!match_char(p, '?')) return cond; // no ternary — return as-is

    ExprVal then_val = parse_nested_ternary(p); // right-associative
    if (then_val.type == VAL_ERR) return then_val;

    if (!match_char(p, ':')) return make_err("missing ':' in ternary");

    ExprVal else_val = parse_nested_ternary(p);
    if (else_val.type == VAL_ERR) return else_val;

    return val_is_truthy(&cond) ? then_val : else_val;
}

// ============================================================================
// Format a numeric result to string
// ============================================================================

static void format_number(double val, char* out, size_t out_len) {
    // If it's an integer value, format without decimals
    if (isfinite(val) && fabs(val) < 1e15 && val == trunc(val)) {
        snprintf(out, out_len, "%lld", (long long)val);
    } else {
        snprintf(out, out_len, "%g", val);
    }
}

static bool format_checked_number(double val, char* out, size_t out_len) {
    char full[64];
    if (fabs(val) <= 9007199254740991.0 && val == trunc(val)) {
        int written = snprintf(full, sizeof(full), "%.0f", val);
        if (written < 0 || static_cast<size_t>(written) >= sizeof(full) ||
            static_cast<size_t>(written) >= out_len) return false;
        memcpy(out, full, written + 1);
        return true;
    }
    for (int precision = 6; precision <= std::numeric_limits<double>::max_digits10; ++precision) {
        int written = snprintf(full, sizeof(full), "%.*g", precision, val);
        if (written < 0 || static_cast<size_t>(written) >= sizeof(full)) return false;
        if (strtod(full, nullptr) == val) {
            if (static_cast<size_t>(written) >= out_len) return false;
            memcpy(out, full, written + 1);
            return true;
        }
    }
    return false;
}

// ============================================================================
// Public API
// ============================================================================

static bool expr_eval_impl(const char* expression, char* out, size_t out_len,
                           expr_datetime_format_fn datetime_format, bool checked) {
    if (!expression || !out || out_len == 0) {
        if (out && out_len > 0) snprintf(out, out_len, "ERR:null");
        return false;
    }

    // Skip leading whitespace
    while (*expression == ' ' || *expression == '\t') expression++;

    if (*expression == '\0') {
        snprintf(out, out_len, "ERR:empty");
        return false;
    }

    Parser parser;
    parser.src = expression;
    parser.pos = 0;
    parser.nesting = 0;
    parser.strings_used = 0;
    parser.datetime_format = datetime_format;
    parser.checked = checked;

    ExprVal result = parse_ternary(&parser);

    // Check for trailing garbage
    skip_ws(&parser);
    if (result.type != VAL_ERR && parser.src[parser.pos] != '\0') {
        result = make_err("trailing chars");
    }

    if (result.type == VAL_ERR) {
        snprintf(out, out_len, "ERR:%s", result.str);
        return false;
    }

    if (result.type == VAL_STR) {
        int written = snprintf(out, out_len, "%.*s", (int)result.str_len, result.str);
        if (checked && (written < 0 || static_cast<size_t>(written) >= out_len)) return false;
    } else {
        if (checked && !isfinite(result.num)) {
            snprintf(out, out_len, "ERR:non-finite");
            return false;
        }
        if (checked) return format_checked_number(result.num, out, out_len);
        format_number(result.num, out, out_len);
    }
    return true;
}

bool expr_eval_with_datetime(const char* expression, char* out, size_t out_len,
                             expr_datetime_format_fn datetime_format) {
    return expr_eval_impl(expression, out, out_len, datetime_format, false);
}

bool expr_eval_checked_with_datetime(const char* expression, char* out, size_t out_len,
                                     expr_datetime_format_fn datetime_format) {
    return expr_eval_impl(expression, out, out_len, datetime_format, true);
}

bool expr_eval(const char* expression, char* out, size_t out_len) {
    return expr_eval_with_datetime(expression, out, out_len, nullptr);
}

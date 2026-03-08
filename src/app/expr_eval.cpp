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

// ============================================================================
// Value type — tagged union for numbers and strings
// ============================================================================

typedef enum { VAL_NUM, VAL_STR, VAL_ERR } ExprValType;

typedef struct {
    ExprValType type;
    double      num;
    char        str[EXPR_STR_MAX];
} ExprVal;

static ExprVal make_num(double v) {
    ExprVal r;
    r.type = VAL_NUM;
    r.num  = v;
    r.str[0] = '\0';
    return r;
}

static ExprVal make_str(const char* s, size_t len) {
    ExprVal r;
    r.type = VAL_STR;
    r.num  = 0;
    if (len >= EXPR_STR_MAX) len = EXPR_STR_MAX - 1;
    memcpy(r.str, s, len);
    r.str[len] = '\0';
    return r;
}

static ExprVal make_err(const char* msg) {
    ExprVal r;
    r.type = VAL_ERR;
    r.num  = 0;
    snprintf(r.str, EXPR_STR_MAX, "ERR:%s", msg);
    return r;
}

static bool val_is_truthy(const ExprVal* v) {
    if (v->type == VAL_NUM) return v->num != 0.0;
    if (v->type == VAL_STR) return v->str[0] != '\0';
    return false;
}

// ============================================================================
// Parser state
// ============================================================================

typedef struct {
    const char* src;
    int         pos;
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

// ============================================================================
// Primary: number, string literal, parenthesized expression
// ============================================================================

static ExprVal parse_primary(Parser* p) {
    skip_ws(p);
    char c = p->src[p->pos];

    // Parenthesized expression
    if (c == '(') {
        p->pos++;
        ExprVal v = parse_ternary(p);
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
    if (p->src[p->pos] == '-') {
        p->pos++;
        ExprVal v = parse_unary(p);
        if (v.type == VAL_ERR) return v;
        if (v.type != VAL_NUM) return make_err("unary - on non-number");
        return make_num(-v.num);
    }
    if (p->src[p->pos] == '+') {
        p->pos++;
        ExprVal v = parse_unary(p);
        if (v.type == VAL_ERR) return v;
        if (v.type != VAL_NUM) return make_err("unary + on non-number");
        return v;
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
            int cmp = strcmp(left.str, right.str);
            return make_num(op == 5 ? (cmp == 0 ? 1.0 : 0.0) : (cmp != 0 ? 1.0 : 0.0));
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

    ExprVal then_val = parse_ternary(p); // right-associative
    if (then_val.type == VAL_ERR) return then_val;

    if (!match_char(p, ':')) return make_err("missing ':' in ternary");

    ExprVal else_val = parse_ternary(p);
    if (else_val.type == VAL_ERR) return else_val;

    return val_is_truthy(&cond) ? then_val : else_val;
}

// ============================================================================
// Format a numeric result to string
// ============================================================================

static void format_number(double val, char* out, size_t out_len) {
    // If it's an integer value, format without decimals
    if (val == (double)(long long)val && fabs(val) < 1e15) {
        snprintf(out, out_len, "%lld", (long long)val);
    } else {
        snprintf(out, out_len, "%g", val);
    }
}

// ============================================================================
// Public API
// ============================================================================

bool expr_eval(const char* expression, char* out, size_t out_len) {
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

    ExprVal result = parse_ternary(&parser);

    // Check for trailing garbage
    skip_ws(&parser);
    if (result.type != VAL_ERR && parser.src[parser.pos] != '\0') {
        result = make_err("trailing chars");
    }

    if (result.type == VAL_ERR) {
        snprintf(out, out_len, "%s", result.str);
        return false;
    }

    if (result.type == VAL_STR) {
        snprintf(out, out_len, "%s", result.str);
    } else {
        format_number(result.num, out, out_len);
    }
    return true;
}

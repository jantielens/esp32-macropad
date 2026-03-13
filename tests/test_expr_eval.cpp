// ============================================================================
// Unit tests for expr_eval — pure expression evaluator
// ============================================================================
// No Arduino, no ESP32, no LVGL. Compiles and runs on any host.
//
// Build:
//   g++ -std=c++17 tests/test_expr_eval.cpp src/app/expr_eval.cpp
//       -o tests/bin/test_expr_eval -lm
// Run:
//   ./tests/bin/test_expr_eval

#include "../src/app/expr_eval.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static int g_pass = 0;
static int g_fail = 0;

static void check_ok(const char* expr, const char* expected, const char* label) {
    char out[128];
    bool ok = expr_eval(expr, out, sizeof(out));
    if (!ok || strcmp(out, expected) != 0) {
        printf("  FAIL [%s]: expr_eval(\"%s\") = \"%s\" (ok=%d), expected ok=true \"%s\"\n",
               label, expr, out, ok, expected);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void check_err(const char* expr, const char* err_prefix, const char* label) {
    char out[128];
    bool ok = expr_eval(expr, out, sizeof(out));
    if (ok || strncmp(out, err_prefix, strlen(err_prefix)) != 0) {
        printf("  FAIL [%s]: expr_eval(\"%s\") = \"%s\" (ok=%d), expected ok=false \"%s...\"\n",
               label, expr, out, ok, err_prefix);
        g_fail++;
    } else {
        g_pass++;
    }
}

// ============================================================================
// Test groups
// ============================================================================

static void test_basic_arithmetic() {
    printf("--- Basic arithmetic ---\n");
    check_ok("1 + 2",           "3",      "int add");
    check_ok("10 - 3",          "7",      "int sub");
    check_ok("4 * 5",           "20",     "int mul");
    check_ok("20 / 4",          "5",      "int div");
    check_ok("10 % 3",          "1",      "int mod");
    check_ok("245760 / 1024",   "240",    "bytes to KB");
    check_ok("1024 * 1024",     "1048576","KB to bytes");
}

static void test_float_arithmetic() {
    printf("--- Float arithmetic ---\n");
    check_ok("3.14 * 2",        "6.28",   "float mul");
    check_ok("10.0 / 3.0",      "3.33333","float div");
    check_ok("0.1 + 0.2",       "0.3",    "float add"); // %g handles this
    check_ok(".5 + .5",         "1",      "leading dot");
}

static void test_operator_precedence() {
    printf("--- Operator precedence ---\n");
    check_ok("2 + 3 * 4",       "14",     "mul before add");
    check_ok("(2 + 3) * 4",     "20",     "parens override");
    check_ok("10 - 2 * 3",      "4",      "mul before sub");
    check_ok("10 / 2 + 3",      "8",      "div before add");
    check_ok("(10 + 2) / 3",    "4",      "parens division");
}

static void test_unary() {
    printf("--- Unary operators ---\n");
    check_ok("-5",               "-5",     "negative");
    check_ok("-5 + 10",          "5",      "neg + pos");
    check_ok("-(3 + 4)",        "-7",     "neg group");
    check_ok("+5",               "5",      "unary plus");
    check_ok("--5",              "5",      "double neg");
}

static void test_comparisons() {
    printf("--- Comparisons ---\n");
    check_ok("5 > 3",           "1",      "gt true");
    check_ok("3 > 5",           "0",      "gt false");
    check_ok("3 < 5",           "1",      "lt true");
    check_ok("5 < 3",           "0",      "lt false");
    check_ok("5 >= 5",          "1",      "gte eq");
    check_ok("5 >= 4",          "1",      "gte gt");
    check_ok("4 >= 5",          "0",      "gte false");
    check_ok("5 <= 5",          "1",      "lte eq");
    check_ok("4 <= 5",          "1",      "lte lt");
    check_ok("5 <= 4",          "0",      "lte false");
    check_ok("5 == 5",          "1",      "eq true");
    check_ok("5 == 6",          "0",      "eq false");
    check_ok("5 != 6",          "1",      "neq true");
    check_ok("5 != 5",          "0",      "neq false");
}

static void test_ternary() {
    printf("--- Ternary operator ---\n");
    check_ok("1 ? \"yes\" : \"no\"",         "yes",   "true string");
    check_ok("0 ? \"yes\" : \"no\"",         "no",    "false string");
    check_ok("5 > 3 ? \"Hot\" : \"OK\"",     "Hot",   "cond true");
    check_ok("2 > 3 ? \"Hot\" : \"OK\"",     "OK",    "cond false");
    check_ok("1 ? 42 : 0",                   "42",    "numeric true");
    check_ok("0 ? 42 : 0",                   "0",     "numeric false");
    // Nested ternary (right-associative)
    check_ok("0 ? \"a\" : 1 ? \"b\" : \"c\"", "b",   "nested ternary");
}

static void test_string_equality() {
    printf("--- String equality ---\n");
    check_ok("\"hello\" == \"hello\"",   "1",    "str eq true");
    check_ok("\"hello\" == \"world\"",   "0",    "str eq false");
    check_ok("\"hello\" != \"world\"",   "1",    "str neq true");
    check_ok("\"hello\" != \"hello\"",   "0",    "str neq false");
}

static void test_realistic_expressions() {
    printf("--- Realistic use cases ---\n");
    // Bytes to kilobytes
    check_ok("245760 / 1024",               "240",    "bytes to KB");
    // Temperature threshold
    check_ok("35 > 30 ? \"Hot\" : \"OK\"",  "Hot",    "temp hot");
    check_ok("22 > 30 ? \"Hot\" : \"OK\"",  "OK",     "temp ok");
    // Power difference (solar - grid)
    check_ok("3200 - 1100",                 "2100",   "power diff");
    // Percentage calculation
    check_ok("(187392 / 262144) * 100",     "71.4844", "percent calc");
}

static void test_edge_cases() {
    printf("--- Edge cases ---\n");
    check_ok("0",               "0",       "zero");
    check_ok("42",              "42",      "single number");
    check_ok("\"hello\"",       "hello",   "single string");
    check_ok("  42  ",          "42",      "whitespace");
    check_ok("((42))",          "42",      "nested parens");
}

static void test_errors() {
    printf("--- Errors ---\n");
    check_err("",                "ERR:",    "empty");
    check_err("1 / 0",          "ERR:",    "div zero");
    check_err("1 % 0",          "ERR:",    "mod zero");
    check_err("(",              "ERR:",    "unclosed paren");
    check_err("\"unclosed",     "ERR:",    "unclosed string");
    check_err("1 +",           "ERR:",    "missing operand");
    check_err("1 2",           "ERR:",    "trailing chars");
    check_err("1 ? 2",         "ERR:",    "incomplete ternary");
}

static void test_threshold_basic() {
    printf("--- threshold() basic ---\n");
    // 1 threshold (2 colors): <50 green, >=50 red
    check_ok("threshold(25, \"#00FF00\", 50, \"#FF0000\")",   "#00FF00", "below T1");
    check_ok("threshold(75, \"#00FF00\", 50, \"#FF0000\")",   "#FF0000", "above T1");
    check_ok("threshold(50, \"#00FF00\", 50, \"#FF0000\")",   "#FF0000", "exactly at T1");

    // 3 thresholds (4 colors): the classic widget pattern
    check_ok("threshold(10, \"#4CAF50\", 33, \"#8BC34A\", 66, \"#FF9800\", 90, \"#F44336\")",
             "#4CAF50", "3T: below T1");
    check_ok("threshold(50, \"#4CAF50\", 33, \"#8BC34A\", 66, \"#FF9800\", 90, \"#F44336\")",
             "#8BC34A", "3T: between T1 and T2");
    check_ok("threshold(80, \"#4CAF50\", 33, \"#8BC34A\", 66, \"#FF9800\", 90, \"#F44336\")",
             "#FF9800", "3T: between T2 and T3");
    check_ok("threshold(95, \"#4CAF50\", 33, \"#8BC34A\", 66, \"#FF9800\", 90, \"#F44336\")",
             "#F44336", "3T: above T3");

    // Exactly at each threshold boundary
    check_ok("threshold(33, \"#4CAF50\", 33, \"#8BC34A\", 66, \"#FF9800\", 90, \"#F44336\")",
             "#8BC34A", "3T: exactly at T1");
    check_ok("threshold(66, \"#4CAF50\", 33, \"#8BC34A\", 66, \"#FF9800\", 90, \"#F44336\")",
             "#FF9800", "3T: exactly at T2");
    check_ok("threshold(90, \"#4CAF50\", 33, \"#8BC34A\", 66, \"#FF9800\", 90, \"#F44336\")",
             "#F44336", "3T: exactly at T3");
}

static void test_threshold_variable_arity() {
    printf("--- threshold() variable arity ---\n");
    // 0 thresholds (1 color): always returns the single color
    check_ok("threshold(42, \"#AABBCC\")", "#AABBCC", "0T: single color");

    // 2 thresholds (3 colors)
    check_ok("threshold(10, \"#111111\", 30, \"#222222\", 70, \"#333333\")",
             "#111111", "2T: below T1");
    check_ok("threshold(50, \"#111111\", 30, \"#222222\", 70, \"#333333\")",
             "#222222", "2T: between");
    check_ok("threshold(90, \"#111111\", 30, \"#222222\", 70, \"#333333\")",
             "#333333", "2T: above T2");

    // 5 thresholds (6 colors) — AQI color bands
    check_ok("threshold(25, \"#00E400\", 50, \"#FFFF00\", 100, \"#FF7E00\", 150, \"#FF0000\", 200, \"#8F3F97\", 300, \"#7E0023\")",
             "#00E400", "5T: tier 0");
    check_ok("threshold(75, \"#00E400\", 50, \"#FFFF00\", 100, \"#FF7E00\", 150, \"#FF0000\", 200, \"#8F3F97\", 300, \"#7E0023\")",
             "#FFFF00", "5T: tier 1");
    check_ok("threshold(125, \"#00E400\", 50, \"#FFFF00\", 100, \"#FF7E00\", 150, \"#FF0000\", 200, \"#8F3F97\", 300, \"#7E0023\")",
             "#FF7E00", "5T: tier 2");
    check_ok("threshold(175, \"#00E400\", 50, \"#FFFF00\", 100, \"#FF7E00\", 150, \"#FF0000\", 200, \"#8F3F97\", 300, \"#7E0023\")",
             "#FF0000", "5T: tier 3");
    check_ok("threshold(250, \"#00E400\", 50, \"#FFFF00\", 100, \"#FF7E00\", 150, \"#FF0000\", 200, \"#8F3F97\", 300, \"#7E0023\")",
             "#8F3F97", "5T: tier 4");
    check_ok("threshold(400, \"#00E400\", 50, \"#FFFF00\", 100, \"#FF7E00\", 150, \"#FF0000\", 200, \"#8F3F97\", 300, \"#7E0023\")",
             "#7E0023", "5T: tier 5");
}

static void test_threshold_expressions_as_value() {
    printf("--- threshold() expression as value ---\n");
    // Value can be an arithmetic expression
    check_ok("threshold(10 + 20, \"#00FF00\", 50, \"#FF0000\")",
             "#00FF00", "expr value below");
    check_ok("threshold(30 + 30, \"#00FF00\", 50, \"#FF0000\")",
             "#FF0000", "expr value above");

    // Negative values
    check_ok("threshold(-10, \"#00FF00\", 0, \"#FF0000\")",
             "#00FF00", "negative value below 0");
    check_ok("threshold(10, \"#00FF00\", 0, \"#FF0000\")",
             "#FF0000", "positive value above 0");

    // Float thresholds
    check_ok("threshold(3.14, \"#AAA\", 2.5, \"#BBB\", 5.0, \"#CCC\")",
             "#BBB", "float threshold between");
}

static void test_threshold_errors() {
    printf("--- threshold() errors ---\n");
    // No arguments
    check_err("threshold()", "ERR:", "no args");
    // Missing color (value only, no color)
    check_err("threshold(42)", "ERR:", "value only no color");
    // Wrong arg count: value + color + threshold but no second color (3 args)
    check_err("threshold(42, \"#AAA\", 50)", "ERR:", "odd args missing final color");
    // Non-numeric threshold
    check_err("threshold(42, \"#AAA\", \"not_a_number\", \"#BBB\")", "ERR:", "non-numeric threshold");
    // Non-numeric value
    check_err("threshold(\"text\", \"#AAA\", 50, \"#BBB\")", "ERR:", "non-numeric value");
    // Non-string color
    check_err("threshold(42, 123, 50, \"#BBB\")", "ERR:", "non-string color");
    // Thresholds not ascending
    check_err("threshold(42, \"#AAA\", 50, \"#BBB\", 30, \"#CCC\")", "ERR:", "non-ascending thresholds");
}

static void test_threshold_in_larger_expression() {
    printf("--- threshold() in expressions ---\n");
    // threshold() result used in string comparison
    check_ok("threshold(25, \"#00FF00\", 50, \"#FF0000\") == \"#00FF00\"",
             "1", "result string cmp true");
    check_ok("threshold(75, \"#00FF00\", 50, \"#FF0000\") == \"#00FF00\"",
             "0", "result string cmp false");
}

static void test_output_buffer_limits() {
    printf("--- Output buffer limits ---\n");
    char tiny[4];
    expr_eval("12345", tiny, sizeof(tiny));
    // Should truncate cleanly
    assert(strlen(tiny) < sizeof(tiny));
    g_pass++;

    char zero[1];
    expr_eval("42", zero, sizeof(zero));
    assert(zero[0] == '\0' || strlen(zero) == 0);
    g_pass++;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== expr_eval unit tests ===\n\n");

    test_basic_arithmetic();
    test_float_arithmetic();
    test_operator_precedence();
    test_unary();
    test_comparisons();
    test_ternary();
    test_string_equality();
    test_realistic_expressions();
    test_edge_cases();
    test_errors();
    test_threshold_basic();
    test_threshold_variable_arity();
    test_threshold_expressions_as_value();
    test_threshold_errors();
    test_threshold_in_larger_expression();
    test_output_buffer_limits();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

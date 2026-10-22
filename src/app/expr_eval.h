#pragma once

// ============================================================================
// Expression Evaluator — Pure C, no Arduino/ESP32 dependencies
// ============================================================================
// Evaluates arithmetic expressions with comparisons and ternary operator.
// Designed to be unit-testable on any host (Linux, macOS, Windows).
//
// Supported syntax:
//   Numbers:      42, 3.14, -7, .5
//   Arithmetic:   +  -  *  /  %
//   Comparisons:  >  <  >=  <=  ==  !=
//   Ternary:      cond ? "text" : "text"   or   cond ? num : num
//   Grouping:     ( expr )
//   Strings:      "quoted text" (valid in ternary branches and == / != comparisons)
//   Unary minus:  -expr
//   Numeric functions: abs(x), min(a, b), max(a, b), clamp(x, low, high),
//                       round(x), floor(x), ceil(x)
//   Threshold function: threshold(value, color0, t1, color1, ..., tN, colorN)
//                       Maps a number to a color string via ascending thresholds.
//                       Returns the color for the matched range, or the last color.
//
// Output is written to `out` (max out_len bytes, null-terminated).
// Returns true on success, false on error (out contains "ERR:xxx").

#include <stddef.h>

#define EXPR_STR_MAX 128       // Max string result length
#define EXPR_MAX_NESTING 12    // Max recursive parser nesting

#ifdef __cplusplus
extern "C" {
#endif

bool expr_eval(const char* expression, char* out, size_t out_len);

#ifdef __cplusplus
}
#endif

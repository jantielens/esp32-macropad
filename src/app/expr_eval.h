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
//
// Output is written to `out` (max out_len bytes, null-terminated).
// Returns true on success, false on error (out contains "ERR:xxx").

#include <stddef.h>

#define EXPR_STR_MAX 128   // Max string result length

#ifdef __cplusplus
extern "C" {
#endif

bool expr_eval(const char* expression, char* out, size_t out_len);

#ifdef __cplusplus
}
#endif

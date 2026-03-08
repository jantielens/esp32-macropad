#pragma once

#include "board_config.h"

// ============================================================================
// Expression Binding — [expr:...] scheme for the binding template engine
// ============================================================================
// Evaluates arithmetic/conditional expressions with embedded binding tokens.
// Inner bindings like [mqtt:...] and [health:...] are resolved first, then
// the resulting expression is evaluated.
//
// Syntax:
//   [expr: [mqtt:topic;path] / 1024 ]
//   [expr: [mqtt:topic;path] > 30 ? "Hot" : "OK" ]
//   [expr: [mqtt:solar;watts] - [mqtt:grid;watts] ]
//   [expr: [health:heap_free] / 1024 ]
//
// Optional format suffix via ';' (last top-level ';' splits expression from format):
//   [expr: [mqtt:topic;path] / 1024 ; %.1f ]
//
// Compile-time gated by HAS_DISPLAY (expressions only make sense on screen).

#ifdef __cplusplus
extern "C" {
#endif

// Register the "expr" scheme with the binding template engine.
// Call once during init, after mqtt/health/time schemes are registered.
void expr_binding_init(void);

#ifdef __cplusplus
}
#endif

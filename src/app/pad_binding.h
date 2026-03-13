#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_MQTT

#include "pad_config.h"
#include <stddef.h>

// ============================================================================
// Pad Binding — [pad:name] scheme for the binding template engine
// ============================================================================
// Resolves page-level named bindings defined in pad config JSON:
//   "bindings": { "power": "[mqtt:solar/power;$.value]" }
//
// Usage in button fields:
//   [pad:power]            — resolve underlying binding, raw value
//   [pad:power;%.2f]       — resolve then apply per-usage printf format
//   [expr:[pad:power]>3000 ? "High" : "Low"]  — works inside expressions
//
// Thread safety: resolve is NOT thread-safe. Call only from the LVGL task.
// Set the active page context before resolving or collecting topics.

#ifdef __cplusplus
extern "C" {
#endif

// Register the "pad" binding scheme. Call once during setup().
void pad_binding_init();

// Set the active page context for resolution and topic collection.
// Must be called before binding_template_resolve() or
// binding_template_collect_topics() when [pad:] bindings may be present.
// page may be NULL (clears context — [pad:] tokens resolve to "---").
void pad_binding_set_page(const PadPageConfig* page);

// Lighter-weight alternative: set bindings directly (for PadScreen which caches
// the bindings array but not the full PadPageConfig).
// bindings may be NULL (clears context). count is ignored when bindings is NULL.
void pad_binding_set_bindings(const PadBinding* bindings, uint8_t count);

// Expand all [pad:name] tokens in a template string to their underlying
// binding templates (text substitution, NOT value resolution).
// Used by data_stream_rebuild() to pre-expand before storing in streams.
// Returns true if any [pad:] token was expanded. out is always null-terminated.
// Requires a page context (pass explicitly, does not use the global context).
bool pad_binding_expand(const PadPageConfig* page, const char* templ,
                        char* out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif // HAS_DISPLAY && HAS_MQTT

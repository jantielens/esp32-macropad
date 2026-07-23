#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_MQTT

#include "pad_config.h"
#include <stddef.h>

// ============================================================================
// Pad Binding — [pad:name] scheme for the binding template engine
// ============================================================================
// Resolves pad-level named bindings defined in pad config JSON:
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

// Set the active pad context for resolution and topic collection.
// Must be called before binding_template_resolve() or
// binding_template_collect_topics() when [pad:] bindings may be present.
// pad may be NULL (clears context — [pad:] tokens resolve to "---").
void pad_binding_set_page(const PadConfig* pad);

// Lighter-weight alternative: set bindings directly (for PadScreen which caches
// the bindings array but not the full PadConfig).
// bindings may be NULL (clears context). count is ignored when bindings is NULL.
void pad_binding_set_bindings(const PadBinding* bindings, uint8_t count);

// Expand all [pad:name] tokens in a template string to their underlying
// binding templates (text substitution, NOT value resolution).
// Used by data_stream_rebuild() to pre-expand before storing in streams.
// Returns true if any [pad:] token was expanded. out is always null-terminated.
// Requires a page context (pass explicitly, does not use the global context).
bool pad_binding_expand(const PadConfig* page, const char* templ,
                        char* out, size_t out_len);

// Resolve `count` binding-template strings against an optional pad binding
// context (binds/bind_count may be NULL/0 — then [pad:] tokens resolve to the
// placeholder). Result i is written to out + i*stride (NUL-terminated, capped
// at stride). Sets the requested context, resolves, then clears it (the live
// pad screen re-establishes its own context every frame, so clearing is safe).
// LVGL/main task only — calls binding_template_resolve(). Powers the MCP
// resolve_bindings tool and the portal /api/pad/resolve preview.
void pad_resolve(const char* const* inputs, size_t count,
                 const PadBinding* binds, uint8_t bind_count,
                 char* out, size_t stride);

#ifdef __cplusplus
}
#endif

#endif // HAS_DISPLAY && HAS_MQTT

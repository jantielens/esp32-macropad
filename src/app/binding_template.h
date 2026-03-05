#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Binding Template Engine
// ============================================================================
// Labels can contain binding tokens: [scheme:params]
// Built-in schemes: "mqtt" and "health"
//   [mqtt:topic;json_path;format]   — live MQTT data
//   [health:key;format]              — local device telemetry
//   "prefix [mqtt:topic;path;fmt] suffix" — mixed static + binding
//
// Additional schemes can be added by registering a resolver
// via binding_template_register().
//
// Thread safety: resolve is NOT thread-safe (uses internal buffers).
// Call only from the LVGL task.

#define BINDING_TEMPLATE_MAX_LEN       192   // Max resolved output length
#define BINDING_MAX_TOKENS             4     // Max binding tokens per label

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------
// Scheme resolver callback
// ----------------------------------------------------------------------------
// Called for each [scheme:params] token during resolve.
// params = everything between "scheme:" and "]"
// out    = buffer to write resolved value into
// out_len = size of out buffer
// Return true if resolved, false if value unavailable (writes placeholder).
typedef bool (*binding_resolver_fn)(const char* params, char* out, size_t out_len);

// ----------------------------------------------------------------------------
// Topic collector callback
// ----------------------------------------------------------------------------
// Called for each [scheme:params] token during topic scanning.
// params = everything between "scheme:" and "]"
typedef void (*binding_topic_collector_fn)(const char* params, void* user_data);

// Register a scheme resolver. scheme is a short name (e.g. "mqtt").
// Max 4 schemes. Returns false if registry is full.
bool binding_template_register(const char* scheme, binding_resolver_fn resolver,
                               binding_topic_collector_fn collector);

// Check if a label string contains any binding tokens [xxx:...]
bool binding_template_has_bindings(const char* label);

// Resolve all binding tokens in a template string.
// Static text is preserved. Binding tokens are replaced with resolver output.
// On resolver failure or unknown scheme, the token is replaced with "---".
// Returns true if any token was resolved. out is always null-terminated.
// Errors produce "ERR:message" in place of the token (never crashes).
bool binding_template_resolve(const char* templ, char* out, size_t out_len);

// Scan a template string and call the collector for each binding token.
// Used by mqtt_sub_store to discover topics to subscribe.
void binding_template_collect_topics(const char* templ, void* user_data);

#ifdef __cplusplus
}
#endif

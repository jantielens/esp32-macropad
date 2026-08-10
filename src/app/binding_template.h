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
#define BINDING_MAX_SCHEMES             16    // Max registered binding schemes

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
// Returns false if the registry is full.
bool binding_template_register(const char* scheme, binding_resolver_fn resolver,
                               binding_topic_collector_fn collector);

// Number of registered binding schemes (for registry-driven enumeration, e.g.
// the MCP capability manifest). The scheme list stays generated — a device
// class registering a scheme auto-appears.
uint8_t binding_template_scheme_count();

// Name of a registered scheme by index (0 .. count-1), or nullptr.
const char* binding_template_scheme_name(uint8_t index);

// Optional per-scheme describe hook for the MCP capability manifest. Each scheme
// describes itself in its own .cpp (mirrors widget describeSchema). The out
// pointer is an ArduinoJson JsonObject* (void* here to keep this widely-included
// header free of the ArduinoJson dependency); the scheme's describe casts it.
typedef void (*binding_describe_fn)(void* out_json);

// Attach a describe hook to a registered scheme (call after register). Returns
// false if the scheme name is unknown.
bool binding_template_set_scheme_describe(const char* scheme, binding_describe_fn fn);

// Invoke scheme[index]'s describe hook into out_json (a JsonObject*). Returns
// true if the scheme has a describe hook, false otherwise.
bool binding_template_describe_scheme(uint8_t index, void* out_json);

// Optional per-scheme validate hook for authoring (MCP write tools). Each scheme
// validates its own token params in its own .cpp (mirrors describe). `params` is
// the text after "scheme:" up to the first ';' '|' or ']' (e.g. the health key,
// timer id, list provider). Returns a human-readable error string (static
// lifetime) when invalid, or nullptr when ok.
typedef const char* (*binding_validate_fn)(const char* params);

// Attach a validate hook to a registered scheme (call after register).
bool binding_template_set_scheme_validate(const char* scheme, binding_validate_fn fn);

// True if `scheme` (length name_len) is a registered scheme name.
bool binding_template_scheme_known(const char* scheme, size_t name_len);

// Run the scheme's validate hook on `params`. Returns nullptr when the scheme is
// unknown, has no validate hook, or the params are valid; otherwise an error.
const char* binding_template_validate_params(const char* scheme, size_t name_len, const char* params);

// Check if a label string contains any binding tokens [xxx:...]
bool binding_template_has_bindings(const char* label);

// Resolve all binding tokens in a template string.
// Static text is preserved. Binding tokens are replaced with resolver output.
// On resolver failure or unknown scheme, the token is replaced with "---".
// Returns true if any token was resolved. out is always null-terminated.
// Errors produce "ERR:message" in place of the token (never crashes).
bool binding_template_resolve(const char* templ, char* out, size_t out_len);

// Resolve a template only when it is exactly one binding token with no
// surrounding static text, e.g. "[health:table]".
// This bypasses the small internal token scratch buffer used by the generic
// resolver and forwards out_len directly to the scheme resolver.
// Returns true when templ matched the exact single-token shape. Output is
// always populated with either resolved content, a fallback, or an error.
bool binding_template_resolve_single_token(const char* templ, char* out, size_t out_len);

// Scan a template string and call the collector for each binding token.
// Used by mqtt_sub_store to discover topics to subscribe.
void binding_template_collect_topics(const char* templ, void* user_data);

#ifdef __cplusplus
}
#endif

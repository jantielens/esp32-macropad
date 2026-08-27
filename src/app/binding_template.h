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
// Return the explicit outcome of resolving this token. Finite-key resolvers
// must return UNKNOWN for unsupported keys and UNAVAILABLE for valid keys
// whose data is not currently available.
enum BindingResolverStatus : uint8_t {
    BINDING_RESOLVER_RESOLVED,
    BINDING_RESOLVER_UNAVAILABLE,
    BINDING_RESOLVER_UNKNOWN,
};

typedef BindingResolverStatus (*binding_resolver_fn)(const char* params, char* out,
                                                      size_t out_len);

// ----------------------------------------------------------------------------
// Topic collector callback
// ----------------------------------------------------------------------------
// Called for each [scheme:params] token during topic scanning.
// params = everything between "scheme:" and "]"
typedef void (*binding_topic_collector_fn)(const char* params, void* user_data);

// A scheme either enumerates finite first-parameter values or explicitly
// declares the first parameter free-form. Dynamic finite sets provide their
// values through these callbacks.
typedef uint8_t (*binding_key_count_fn)(void);
typedef const char* (*binding_key_at_fn)(uint8_t index);

enum BindingValidationMode : uint8_t {
    BINDING_VALIDATION_STANDARD,
    BINDING_VALIDATION_EXPRESSION,
    BINDING_VALIDATION_STRUCTURAL_ONLY,
};

struct BindingSchemeSpec {
    uint8_t min_params;
    uint8_t max_params;
    uint8_t widget_max_params;
    int8_t format_param;
    BindingValidationMode validation_mode;
    bool free_form;
    binding_key_count_fn key_count;
    binding_key_at_fn key_at;
};

// Register a scheme resolver and its complete metadata contract. The metadata
// is consumed by firmware validation, the portal API, and MCP capabilities.
// Returns false if the registry is full or the spec is incomplete.
bool binding_template_register(const char* scheme, binding_resolver_fn resolver,
                               binding_topic_collector_fn collector,
                               const BindingSchemeSpec& spec);

// Number of registered binding schemes (for registry-driven enumeration, e.g.
// the MCP capability manifest). The scheme list stays generated — a device
// class registering a scheme auto-appears.
uint8_t binding_template_scheme_count();

// Name of a registered scheme by index (0 .. count-1), or nullptr.
const char* binding_template_scheme_name(uint8_t index);

// Return a registered scheme's metadata, or nullptr when index is invalid.
const BindingSchemeSpec* binding_template_scheme_spec(uint8_t index);

// Resolve one complete parameter string through the registered scheme.
// UNKNOWN means the scheme or its parameters are not recognized by its
// resolver. The resolver owns finite-key recognition so its implementation and
// declared metadata cannot drift silently.
BindingResolverStatus binding_template_resolve_registered(const char* scheme, size_t name_len,
                                                          const char* params, char* out,
                                                          size_t out_len);

// True if `scheme` (length name_len) is a registered scheme name.
bool binding_template_scheme_known(const char* scheme, size_t name_len);

// Validate a complete scheme parameter string against the registry metadata.
// Returns nullptr when valid or unknown; otherwise a static error string.
const char* binding_template_validate_params(const char* scheme, size_t name_len, const char* params);

// Check if a label string contains any binding tokens [xxx:...]
bool binding_template_has_bindings(const char* label);

// Resolve all binding tokens in a template string.
// Static text is preserved. Binding tokens are replaced with resolver output.
// On resolver failure or unknown scheme, the token is replaced with "---".
// Returns true if any token was resolved. out is always null-terminated.
// Errors produce "ERR:message" in place of the token (never crashes).
bool binding_template_resolve(const char* templ, char* out, size_t out_len);

// Resolve all tokens and report the aggregate resolver outcome. Output uses the
// same fallback rules as binding_template_resolve().
BindingResolverStatus binding_template_resolve_status(const char* templ, char* out,
                                                      size_t out_len);

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

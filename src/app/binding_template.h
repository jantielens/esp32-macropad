#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Binding Template Engine
// ============================================================================
// Labels can contain binding tokens: [scheme:params]
//   "prefix [mqtt:topic;path;fmt] suffix" — mixed static + binding
//
// Schemes register a resolver plus metadata and user-facing docs via
// binding_template_register(). GET /api/bindings?docs=1 and the MCP
// capability manifest serialize the registry, so only compiled-in schemes
// are documented.
//
// Thread safety: resolve is NOT thread-safe (uses internal buffers).
// Call only from the LVGL task.

#define BINDING_TEMPLATE_MAX_LEN       192   // Max resolved output length
#define BINDING_MAX_TOKENS             4     // Max binding tokens per label
#define BINDING_MAX_SCHEMES             24    // Max registered binding schemes

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

// User-facing scheme documentation, rendered by the portal binding reference
// and the MCP manifest. Key patterns use '#' for one or more digits and '*'
// for one or more characters other than '.' or ';'.
struct BindingParamDoc {
    const char* name;
    const char* desc;
    const char* default_value;

    constexpr BindingParamDoc(const char* param_name = nullptr,
                              const char* description = nullptr,
                              const char* default_text = nullptr)
        : name(param_name), desc(description), default_value(default_text) {}
};

struct BindingKeyDoc {
    const char* key;
    const char* desc;
    const char* group;  // optional
};

struct BindingExampleDoc {
    const char* code;
    const char* desc;
};

enum BindingAuthoringValueKind : uint8_t {
    BINDING_AUTHORING_KIND_INTEGER,
    BINDING_AUTHORING_KIND_TEXT,
    BINDING_AUTHORING_KIND_NUMBER,
};

struct BindingReferenceDoc {
    const char* group;    // optional
    const char* syntax;
    const char* sample;   // optional example output
    const char* desc;
    const char* example;  // optional complete token
    const char* unit;  // nullptr = unknown, "" = unitless
    BindingAuthoringValueKind kind;

    constexpr BindingReferenceDoc(const char* reference_group = nullptr,
                                  const char* reference_syntax = nullptr,
                                  const char* sample_output = nullptr,
                                  const char* description = nullptr,
                                  const char* example_token = nullptr,
                                  const char* source_unit = "",
                                  BindingAuthoringValueKind value_kind = BINDING_AUTHORING_KIND_TEXT)
        : group(reference_group), syntax(reference_syntax), sample(sample_output),
          desc(description), example(example_token), unit(source_unit), kind(value_kind) {}
};

// Returns nullptr when the scheme can resolve, else a short reason.
typedef const char* (*binding_status_fn)(void);

struct BindingSchemeDoc {
    const char* category;
    const char* summary;
    const BindingParamDoc* params;  // one entry per max_params
    uint8_t param_count;
    const BindingKeyDoc* keys;
    uint8_t key_doc_count;
    const BindingExampleDoc* examples;
    uint8_t example_count;
    const char* reference_title;     // optional
    bool reference_is_formats;
    const BindingReferenceDoc* reference;
    uint8_t reference_count;
    const char* note;                // optional
    binding_status_fn status;        // optional
};

enum BindingAuthoringFormat : uint8_t {
    BINDING_AUTHORING_FORMAT_RAW = 1 << 0,
    BINDING_AUTHORING_FORMAT_INTEGER = 1 << 1,
    BINDING_AUTHORING_FORMAT_NUMBER = 1 << 2,
};

enum BindingAuthoringCapability : uint8_t {
    BINDING_AUTHORING_CAP_DISPLAY = 1 << 0,
    BINDING_AUTHORING_CAP_NUMERIC_WIDGET = 1 << 1,
    BINDING_AUTHORING_CAP_LIVE_SAMPLING = 1 << 2,
};

struct BindingAuthoringKey {
    const char* key;
    BindingAuthoringValueKind kind;
    const char* unit;  // nullptr = unknown, "" = known to have no unit
    uint8_t formats;
    const char* integer_format;
    uint8_t capabilities;
    bool read_only;
    const char* example_value;

    constexpr BindingAuthoringKey(const char* binding_key = nullptr,
                                  BindingAuthoringValueKind value_kind = BINDING_AUTHORING_KIND_INTEGER,
                                  const char* source_unit = nullptr,
                                  uint8_t supported_formats = 0,
                                  const char* integer_format_text = nullptr,
                                  uint8_t supported_capabilities = 0,
                                  bool is_read_only = false,
                                  const char* example = nullptr)
        : key(binding_key), kind(value_kind), unit(source_unit), formats(supported_formats),
          integer_format(integer_format_text), capabilities(supported_capabilities),
          read_only(is_read_only), example_value(example) {}
};

constexpr BindingAuthoringKey binding_authoring_text(
    const char* key, const char* example, const char* unit = "") {
    return {key, BINDING_AUTHORING_KIND_TEXT, unit, BINDING_AUTHORING_FORMAT_RAW,
            nullptr, BINDING_AUTHORING_CAP_DISPLAY, true, example};
}

constexpr BindingAuthoringKey binding_authoring_integer(
    const char* key, const char* unit, const char* example, const char* format = "") {
    return {key, BINDING_AUTHORING_KIND_INTEGER, unit,
            BINDING_AUTHORING_FORMAT_RAW | BINDING_AUTHORING_FORMAT_INTEGER, format,
            BINDING_AUTHORING_CAP_DISPLAY | BINDING_AUTHORING_CAP_NUMERIC_WIDGET |
                BINDING_AUTHORING_CAP_LIVE_SAMPLING,
            true, example};
}

constexpr BindingAuthoringKey binding_authoring_number(
    const char* key, const char* unit, const char* example, bool formatted = true) {
    return {key, BINDING_AUTHORING_KIND_NUMBER, unit,
            static_cast<uint8_t>(BINDING_AUTHORING_FORMAT_RAW |
                (formatted ? BINDING_AUTHORING_FORMAT_NUMBER : 0)), nullptr,
            BINDING_AUTHORING_CAP_DISPLAY | BINDING_AUTHORING_CAP_NUMERIC_WIDGET |
                BINDING_AUTHORING_CAP_LIVE_SAMPLING,
            true, example};
}

struct BindingAuthoringSpec {
    const BindingAuthoringKey* keys;
    uint8_t key_count;
};

#define BINDING_DOC_LIST(array) (array), (uint8_t)(sizeof(array) / sizeof((array)[0]))
#define BINDING_DOC_NONE nullptr, 0

// True when a documented key or pattern matches a concrete key.
bool binding_key_doc_matches(const char* pattern, const char* key);

struct BindingSchemeSpec {
    uint8_t min_params;
    uint8_t max_params;
    uint8_t widget_max_params;
    int8_t format_param;
    BindingValidationMode validation_mode;
    bool free_form;
    binding_key_count_fn key_count;
    binding_key_at_fn key_at;
    const BindingSchemeDoc* doc;
    const BindingAuthoringSpec* authoring;

    constexpr BindingSchemeSpec(uint8_t minimum_params = 0, uint8_t maximum_params = 0,
                                uint8_t widget_params = 0, int8_t format_index = 0,
                                BindingValidationMode validation = BINDING_VALIDATION_STANDARD,
                                bool is_free_form = false,
                                binding_key_count_fn count_keys = nullptr,
                                binding_key_at_fn key_at_index = nullptr,
                                const BindingSchemeDoc* documentation = nullptr,
                                const BindingAuthoringSpec* authoring_spec = nullptr)
        : min_params(minimum_params), max_params(maximum_params), widget_max_params(widget_params),
          format_param(format_index), validation_mode(validation), free_form(is_free_form),
          key_count(count_keys), key_at(key_at_index), doc(documentation), authoring(authoring_spec) {}
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
// Dialog/action safety path: reject resolver failures and truncated output.
bool binding_template_resolve_checked(const char* templ, char* out, size_t out_len);

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

// Visit tokens (including expression children) without invoking transport-owned
// collectors. Runs on the binding/LVGL task like resolution.
typedef void (*binding_token_visitor_fn)(const char* scheme, size_t scheme_len,
                                         const char* params, void* context);
void binding_template_visit_tokens(const char* templ, binding_token_visitor_fn visitor,
                                   void* context);

#ifdef __cplusplus
}
#endif

#include "pad_binding.h"

#if HAS_DISPLAY

#include "binding_template.h"
#include "log_manager.h"
#if HAS_MQTT
#include "mqtt_sub_store.h"
#endif
#include "binding_value.h"
#include "expr_binding.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "PadBind"

// ============================================================================
// Page context — set before resolve/collect, single-threaded (LVGL task)
// ============================================================================

static const PadBinding* g_bindings = nullptr;
static uint8_t g_binding_count = 0;
static thread_local const PadBindingScope* s_override = nullptr;

PadBindingScope::PadBindingScope(const PadBinding* bindings, uint8_t count, bool enabled)
    : bindings_(bindings), count_(bindings ? count : 0), enabled_(enabled), previous_(s_override) {
    if (enabled_) s_override = this;
}

PadBindingScope::~PadBindingScope() { if (enabled_) s_override = previous_; }

void pad_binding_effective(const PadBinding** bindings, uint8_t* count) {
    if (bindings) *bindings = s_override ? s_override->bindings_ : g_bindings;
    if (count) *count = s_override ? s_override->count_ : g_binding_count;
}

void pad_binding_set_page(const PadConfig* page) {
    if (page) {
        g_bindings = page->bindings;
        g_binding_count = page->binding_count;
    } else {
        g_bindings = nullptr;
        g_binding_count = 0;
    }
}

void pad_binding_set_bindings(const PadBinding* bindings, uint8_t count) {
    g_bindings = bindings;
    g_binding_count = bindings ? count : 0;
}

void pad_binding_get_bindings(const PadBinding** bindings, uint8_t* count) {
    pad_binding_effective(bindings, count);
}

// ============================================================================
// Helpers
// ============================================================================

// Look up a named binding in a bindings array. Returns the template string or NULL.
const char* pad_binding_find_template(const PadBinding* bindings, uint8_t count,
                                      const char* name, size_t name_len) {
    if (!bindings) return nullptr;
    for (uint8_t i = 0; i < count; i++) {
        if (strlen(bindings[i].name) == name_len &&
            strncmp(bindings[i].name, name, name_len) == 0) {
            return bindings[i].value;
        }
    }
    return nullptr;
}

// Split params into name and optional format: "name" or "name;format"
// Sets *name_len to length of the name portion.
// Returns pointer to format string (after ';') or NULL if no format.
static const char* split_name_format(const char* params, size_t* name_len) {
    const char* semi = strchr(params, ';');
    if (semi) {
        *name_len = (size_t)(semi - params);
        return semi + 1;  // format string after ';'
    }
    *name_len = strlen(params);
    return nullptr;
}

struct PadBindingTraversal {
    uint8_t depth = 0;
    uint8_t remaining = 0;
    bool limited = false;
    const char* active[PAD_BINDING_MAX_CHAIN_DEPTH] = {};
};

class PadBindingGuard {
public:
    PadBindingGuard(PadBindingTraversal& traversal, const char* templ) : traversal_(traversal) {
        if (!traversal_.depth) {
            traversal_.remaining = PAD_BINDING_MAX_EVALUATIONS;
            traversal_.limited = false;
        }
        entered_ = traversal_.depth < PAD_BINDING_MAX_CHAIN_DEPTH && traversal_.remaining;
        for (uint8_t i = 0; entered_ && i < traversal_.depth; ++i)
            entered_ = traversal_.active[i] != templ;
        if (entered_) {
            traversal_.active[traversal_.depth] = templ;
            ++traversal_.depth;
            --traversal_.remaining;
        } else traversal_.limited = true;
    }
    ~PadBindingGuard() { if (entered_) --traversal_.depth; }
    explicit operator bool() const { return entered_; }
private:
    PadBindingTraversal& traversal_;
    bool entered_;
};

static thread_local PadBindingTraversal s_resolution;
static thread_local PadBindingTraversal s_collection;
static thread_local PadBindingTraversal s_value_kind;

static BindingResolvedValueKind pad_binding_value_kind(const char* params) {
    if (!params || strchr(params, ';')) return BINDING_VALUE_KIND_UNKNOWN;
    const PadBinding* bindings = nullptr;
    uint8_t count = 0;
    pad_binding_effective(&bindings, &count);
    const char* templ = pad_binding_find_template(bindings, count, params, strlen(params));
    if (!templ) return BINDING_VALUE_KIND_UNKNOWN;
    PadBindingGuard guard(s_value_kind, templ);
    if (!guard) return BINDING_VALUE_KIND_UNKNOWN;
    return binding_template_value_kind(templ);
}

// ============================================================================
// Scheme resolver — called by binding_template_resolve() for [pad:...]
// ============================================================================

static BindingResolverStatus pad_binding_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        strlcpy(out, "ERR:no_name", out_len);  // debug only — engine overwrites with "---"
        return BINDING_RESOLVER_UNAVAILABLE;
    }

    const PadBinding* bindings = nullptr;
    uint8_t count = 0;
    pad_binding_effective(&bindings, &count);
    if (!bindings) {
        strlcpy(out, "ERR:no_page", out_len);  // debug only — engine overwrites with "---"
        return BINDING_RESOLVER_UNAVAILABLE;
    }

    // Split "name" or "name;format"
    size_t name_len = 0;
    const char* fmt = split_name_format(params, &name_len);

    // Look up the binding
    const char* tmpl = pad_binding_find_template(bindings, count, params, name_len);
    if (!tmpl) {
        strlcpy(out, "ERR:not_found", out_len);  // debug only — engine overwrites with "---"
        return BINDING_RESOLVER_UNAVAILABLE;
    }

    PadBindingGuard guard(s_resolution, tmpl);
    if (!guard) {
        strlcpy(out, "ERR:pad_limit", out_len);
        return BINDING_RESOLVER_UNAVAILABLE;
    }

    // Resolve the underlying binding template
    char resolved[BINDING_TEMPLATE_MAX_LEN];
    const BindingResolverStatus status = strstr(tmpl, "[pad:")
        ? (binding_template_resolve_checked(tmpl, resolved, sizeof(resolved))
            ? BINDING_RESOLVER_RESOLVED : BINDING_RESOLVER_UNAVAILABLE)
        : binding_template_resolve_status(tmpl, resolved, sizeof(resolved));
    if (status != BINDING_RESOLVER_RESOLVED) return status;

    // Apply per-usage format if provided
    if (fmt && fmt[0]) {
        binding_value_format(resolved, fmt, out, out_len);
    } else {
        strlcpy(out, resolved, out_len);
    }

    return BINDING_RESOLVER_RESOLVED;
}

// ============================================================================
// Topic collector — called by binding_template_collect_topics() for [pad:...]
// ============================================================================

void pad_binding_visit_dependencies(const char* params,
                                     void (*visitor)(const char*, void*), void* context) {
    const PadBinding* bindings = nullptr;
    uint8_t count = 0;
    pad_binding_effective(&bindings, &count);
    if (!params || !params[0] || !bindings || !visitor) return;

    size_t name_len = 0;
    split_name_format(params, &name_len);

    const char* tmpl = pad_binding_find_template(bindings, count, params, name_len);
    if (!tmpl || !tmpl[0]) return;

    PadBindingGuard guard(s_collection, tmpl);
    if (!guard) return;

    visitor(tmpl, context);
    if (s_collection.depth == 1 && s_collection.limited)
        LOGW(TAG, "Dependency collection stopped at pad binding cycle/nesting/work limit");
}

static void pad_binding_collect(const char* params, void* user_data) {
    pad_binding_visit_dependencies(params, binding_template_collect_topics, user_data);
}

// ============================================================================
// Expand utility — text substitution of [pad:name] to underlying templates
// ============================================================================

static bool expand_bindings(const PadBinding* bindings, uint8_t count,
                             const char* templ, char* out, size_t out_len,
                             size_t& written, bool& any_expanded,
                             PadBindingTraversal& traversal) {
    const char* p = templ;
    while (*p) {
        const char* start = strstr(p, "[pad:");
        const size_t prefix = start ? static_cast<size_t>(start - p) : strlen(p);
        if (prefix >= out_len - written) return false;
        memcpy(out + written, p, prefix);
        written += prefix;
        if (!start) break;

        // Find closing ']' with bracket depth tracking
        const char* q = start + 5;
        int depth = 1;
        while (*q && depth > 0) {
            if (*q == '[') depth++;
            else if (*q == ']') depth--;
            q++;
        }

        if (depth != 0) return false;

        const char* params_start = start + 5;
        const size_t name_len = static_cast<size_t>(q - 1 - params_start);
        // Formatting and fallback apply to the resolved alias, not its source.
        if (memchr(params_start, ';', name_len) || memchr(params_start, '|', name_len))
            return false;
        const char* binding_tmpl = pad_binding_find_template(bindings, count, params_start, name_len);
        if (!binding_tmpl) return false;
        PadBindingGuard guard(traversal, binding_tmpl);
        if (!guard || !expand_bindings(bindings, count, binding_tmpl, out, out_len,
                                        written, any_expanded, traversal)) return false;
        any_expanded = true;
        p = q;
    }

    return true;
}

PadBindingExpandStatus pad_binding_expand_bindings(const PadBinding* bindings, uint8_t count,
                                                   const char* templ, char* out, size_t out_len) {
    if (!templ || !out || !out_len) {
        if (out && out_len) out[0] = '\0';
        LOGW(TAG, "Invalid pad binding expansion buffer");
        return PadBindingExpandStatus::UNAVAILABLE;
    }
    PadBindingTraversal traversal;
    size_t written = 0;
    bool any_expanded = false;
    if (!expand_bindings(bindings, count, templ, out, out_len, written, any_expanded, traversal)) {
        strlcpy(out, templ, out_len);
        LOGW(TAG, "Pad binding expansion unavailable (missing reference, cycle/nesting/work limit, format/fallback or capacity)");
        return PadBindingExpandStatus::UNAVAILABLE;
    }
    out[written] = '\0';
    return any_expanded ? PadBindingExpandStatus::EXPANDED : PadBindingExpandStatus::UNCHANGED;
}

PadBindingExpandStatus pad_binding_expand(const PadConfig* page, const char* templ,
                                          char* out, size_t out_len) {
    if (!page) {
        return pad_binding_expand_bindings(nullptr, 0, templ, out, out_len);
    }
    return pad_binding_expand_bindings(page->bindings, page->binding_count,
                                       templ, out, out_len);
}

// ============================================================================
// Batch resolve — set context, resolve N templates, restore
// ============================================================================

void pad_resolve(const char* const* inputs, size_t count,
                 const PadBinding* binds, uint8_t bind_count,
                 char* out, size_t stride) {
    if (!inputs || !out || stride == 0) return;
    PadBindingScope scope(binds, bind_count);
    for (size_t i = 0; i < count; i++) {
        char* dst = out + i * stride;
        const char* in = inputs[i];
        if (!in) { dst[0] = '\0'; continue; }
        // Live-preview aid: if this input references an MQTT topic the device is
        // not yet subscribed to (e.g. a binding the user is typing that isn't
        // saved), subscribe now so it resolves on a subsequent refresh once the
        // (retained) message arrives. Reconciled by the next subscribe_all().
#if HAS_MQTT
        mqtt_sub_store_ensure_binding_subscribed(in);
#endif
        binding_template_resolve(in, dst, stride);
    }
}

// ============================================================================
// Init
// ============================================================================

static const BindingParamDoc kPadParams[] = {
    {"name", "Name from the pad's Bindings list."},
    {"format", "printf float format such as %.1f, or %s for text."},
};

static const BindingDefinitionConstraints kPadDefinitionConstraints = {
    PAD_MAX_BINDINGS, PAD_BINDING_NAME_MAX_LEN - 1, CONFIG_LABEL_MAX_LEN - 1,
    "^[A-Za-z][A-Za-z0-9_]*$", "same_pad", true,
    PAD_BINDING_MAX_CHAIN_DEPTH, PAD_BINDING_MAX_EVALUATIONS, false, EXPR_BINDING_MAX_DEPTH,
};

static const BindingAuthoringSpec kPadAuthoring = {
    nullptr, 0, &kPadDefinitionConstraints,
};

static const BindingExampleDoc kPadExamples[] = {
    {"[pad:power]", "Raw value of the named pad binding."},
    {"[pad:power;%.0f W]", "Local format without redefining the source."},
    {"[expr:[pad:power] > 3000 ? \"High\" : \"Low\"]", "Reuse a named value inside an expression."},
};

static const BindingSchemeDoc kPadDoc = {
    "Logic", "Reuse a binding defined once in the pad's Bindings list.",
    BINDING_DOC_LIST(kPadParams), BINDING_DOC_NONE, BINDING_DOC_LIST(kPadExamples),
    nullptr, false, BINDING_DOC_NONE,
    "Requires the same pad's binding context; not available in shared device defaults or context-free hosts. "
    "Up to 16 definitions; names use an ASCII letter followed by ASCII letters, digits or underscores (max 31 bytes). "
    "Each saved value is limited to 191 UTF-8 bytes and may reference other names in the same effective pad, in any definition order. "
    "Runtime allows two named bindings per chain (including the starting name) and 64 named evaluations per lookup. "
    "Cycles stop at the first repeated active name. Up to five expression bindings and twelve arithmetic nesting levels are allowed, "
    "subject to the calling task's available stack; insufficient stack makes a binding unavailable. "
    "All schemes also share a ten-active-resolver and 256-evaluations-per-root safety guard. "
    "Dependencies are not checked on save: missing names, cycles and excessive depth/work become unavailable at runtime (--- or an explicit fallback). "
    "History supports raw, unformatted pad references whose fully expanded template fits 191 bytes; per-use pad formats/fallbacks remain runtime-only. "
    "Named bindings shorten references in smaller fields; they do not increase template, resolved-output or destination-field limits.", nullptr,
};

void pad_binding_init() {
    if (!binding_template_register("pad", pad_binding_resolve, pad_binding_collect,
                                   {1, 2, 1, 1, BINDING_VALIDATION_STANDARD, true, nullptr, nullptr,
                                    &kPadDoc, &kPadAuthoring, nullptr, nullptr, pad_binding_value_kind})) {
        LOGE(TAG, "Failed to register pad binding scheme");
    }
}

#endif // HAS_DISPLAY

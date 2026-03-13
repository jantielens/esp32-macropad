#include "pad_binding.h"

#if HAS_DISPLAY && HAS_MQTT

#include "binding_template.h"
#include "log_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "PadBind"

// ============================================================================
// Page context — set before resolve/collect, single-threaded (LVGL task)
// ============================================================================

static const PadBinding* g_bindings = nullptr;
static uint8_t g_binding_count = 0;

void pad_binding_set_page(const PadPageConfig* page) {
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

// ============================================================================
// Helpers
// ============================================================================

// Look up a named binding in a bindings array. Returns the template string or NULL.
static const char* find_binding(const PadBinding* bindings, uint8_t count,
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

// ============================================================================
// Scheme resolver — called by binding_template_resolve() for [pad:...]
// ============================================================================

static bool pad_binding_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        strlcpy(out, "ERR:no_name", out_len);  // debug only — engine overwrites with "---"
        return false;
    }

    if (!g_bindings) {
        strlcpy(out, "ERR:no_page", out_len);  // debug only — engine overwrites with "---"
        return false;
    }

    // Split "name" or "name;format"
    size_t name_len = 0;
    const char* fmt = split_name_format(params, &name_len);

    // Look up the binding
    const char* tmpl = find_binding(g_bindings, g_binding_count, params, name_len);
    if (!tmpl) {
        strlcpy(out, "ERR:not_found", out_len);  // debug only — engine overwrites with "---"
        return false;
    }

    // Guard against recursive [pad:] references in the underlying template
    if (strstr(tmpl, "[pad:")) {
        strlcpy(out, "ERR:recursive", out_len);  // debug only — engine overwrites with "---"
        return false;
    }

    // Resolve the underlying binding template
    char resolved[BINDING_TEMPLATE_MAX_LEN];
    binding_template_resolve(tmpl, resolved, sizeof(resolved));

    // If resolution produced a placeholder, pass it through
    if (strstr(resolved, "---") != nullptr) {
        strlcpy(out, resolved, out_len);
        return false;
    }

    // Apply per-usage format if provided
    if (fmt && fmt[0]) {
        // Try numeric format first
        char* end = nullptr;
        double val = strtod(resolved, &end);
        if (end != resolved && *end == '\0') {
            // Numeric value — apply printf format
            snprintf(out, out_len, fmt, val);
        } else {
            // String value — apply %s format
            snprintf(out, out_len, fmt, resolved);
        }
    } else {
        strlcpy(out, resolved, out_len);
    }

    return true;
}

// ============================================================================
// Topic collector — called by binding_template_collect_topics() for [pad:...]
// ============================================================================

static void pad_binding_collect(const char* params, void* user_data) {
    if (!params || !params[0] || !g_bindings) return;

    size_t name_len = 0;
    split_name_format(params, &name_len);

    const char* tmpl = find_binding(g_bindings, g_binding_count, params, name_len);
    if (!tmpl || !tmpl[0]) return;

    // Guard against recursive [pad:] in underlying template
    if (strstr(tmpl, "[pad:")) return;

    // Delegate to binding_template_collect_topics to discover nested topics
    binding_template_collect_topics(tmpl, user_data);
}

// ============================================================================
// Expand utility — text substitution of [pad:name] to underlying templates
// ============================================================================

bool pad_binding_expand(const PadPageConfig* page, const char* templ,
                        char* out, size_t out_len) {
    if (!templ || !out || out_len == 0) {
        if (out && out_len > 0) out[0] = '\0';
        return false;
    }
    if (!page || page->binding_count == 0) {
        strlcpy(out, templ, out_len);
        return false;
    }

    const PadBinding* bindings = page->bindings;
    uint8_t count = page->binding_count;

    bool any_expanded = false;
    size_t written = 0;
    const char* p = templ;

    while (*p && written < out_len - 1) {
        // Look for [pad: token
        const char* start = strstr(p, "[pad:");
        if (!start) {
            // No more [pad:] tokens — copy rest
            size_t remain = strlen(p);
            size_t space = out_len - 1 - written;
            size_t copy = remain < space ? remain : space;
            memcpy(out + written, p, copy);
            written += copy;
            break;
        }

        // Copy text before the token
        size_t prefix = (size_t)(start - p);
        if (prefix > 0) {
            size_t space = out_len - 1 - written;
            size_t copy = prefix < space ? prefix : space;
            memcpy(out + written, p, copy);
            written += copy;
        }

        // Find closing ']' with bracket depth tracking
        const char* q = start + 5;  // past "[pad:"
        int depth = 1;
        while (*q && depth > 0) {
            if (*q == '[') depth++;
            else if (*q == ']') depth--;
            q++;
        }

        if (depth != 0) {
            // Unclosed bracket — copy as-is and move on
            size_t tlen = (size_t)(q - start);
            size_t space = out_len - 1 - written;
            size_t copy = tlen < space ? tlen : space;
            memcpy(out + written, start, copy);
            written += copy;
            p = q;
            continue;
        }

        // Extract name (and optional format) between "pad:" and "]"
        const char* params_start = start + 5;
        size_t params_len = (size_t)(q - 1 - params_start);

        // Extract just the name portion (before ';')
        size_t name_len = params_len;
        const char* semi = (const char*)memchr(params_start, ';', params_len);
        if (semi) name_len = (size_t)(semi - params_start);

        // Look up binding
        const char* binding_tmpl = find_binding(bindings, count, params_start, name_len);
        if (binding_tmpl && !strstr(binding_tmpl, "[pad:")) {
            // If there's a format suffix, we can't simply substitute the template —
            // the format applies to the resolved value, not the template text.
            // For expand, we only substitute when there's NO format suffix,
            // or keep the full [pad:name;fmt] token as-is for runtime resolution.
            if (!semi) {
                // No format — direct template substitution
                size_t blen = strlen(binding_tmpl);
                size_t space = out_len - 1 - written;
                size_t copy = blen < space ? blen : space;
                memcpy(out + written, binding_tmpl, copy);
                written += copy;
                any_expanded = true;
            } else {
                // Has format — keep as [pad:name;fmt] for runtime resolution
                size_t tlen = (size_t)(q - start);
                size_t space = out_len - 1 - written;
                size_t copy = tlen < space ? tlen : space;
                memcpy(out + written, start, copy);
                written += copy;
            }
        } else {
            // Unknown binding or recursive — copy token as-is
            size_t tlen = (size_t)(q - start);
            size_t space = out_len - 1 - written;
            size_t copy = tlen < space ? tlen : space;
            memcpy(out + written, start, copy);
            written += copy;
        }

        p = q;
    }

    out[written] = '\0';
    return any_expanded;
}

// ============================================================================
// Init
// ============================================================================

void pad_binding_init() {
    if (!binding_template_register("pad", pad_binding_resolve, pad_binding_collect)) {
        LOGE(TAG, "Failed to register pad binding scheme");
    }
}

#endif // HAS_DISPLAY && HAS_MQTT

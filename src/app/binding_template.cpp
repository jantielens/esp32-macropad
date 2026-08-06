#include "binding_template.h"
#include "log_manager.h"

#include <string.h>
#include <stdio.h>

#define TAG "BindTpl"

// ============================================================================
// Scheme registry
// ============================================================================

#define MAX_SCHEMES 9

struct SchemeEntry {
    char name[16];
    binding_resolver_fn resolver;
    binding_topic_collector_fn collector;
    binding_describe_fn describe;
    binding_validate_fn validate;
};

static SchemeEntry g_schemes[MAX_SCHEMES];
static int g_scheme_count = 0;

bool binding_template_register(const char* scheme, binding_resolver_fn resolver,
                               binding_topic_collector_fn collector) {
    if (!scheme || !scheme[0] || g_scheme_count >= MAX_SCHEMES) return false;
    SchemeEntry& e = g_schemes[g_scheme_count];
    strlcpy(e.name, scheme, sizeof(e.name));
    e.resolver = resolver;
    e.collector = collector;
    e.describe = nullptr;
    e.validate = nullptr;
    g_scheme_count++;
    return true;
}

uint8_t binding_template_scheme_count() { return (uint8_t)g_scheme_count; }

const char* binding_template_scheme_name(uint8_t index) {
    return (index < g_scheme_count) ? g_schemes[index].name : nullptr;
}

bool binding_template_set_scheme_describe(const char* scheme, binding_describe_fn fn) {
    if (!scheme) return false;
    for (int i = 0; i < g_scheme_count; i++) {
        if (strcmp(g_schemes[i].name, scheme) == 0) { g_schemes[i].describe = fn; return true; }
    }
    return false;
}

bool binding_template_describe_scheme(uint8_t index, void* out_json) {
    if (index >= g_scheme_count || !g_schemes[index].describe) return false;
    g_schemes[index].describe(out_json);
    return true;
}

bool binding_template_set_scheme_validate(const char* scheme, binding_validate_fn fn) {
    if (!scheme) return false;
    for (int i = 0; i < g_scheme_count; i++) {
        if (strcmp(g_schemes[i].name, scheme) == 0) { g_schemes[i].validate = fn; return true; }
    }
    return false;
}

bool binding_template_scheme_known(const char* scheme, size_t name_len) {
    for (int i = 0; i < g_scheme_count; i++) {
        if (strlen(g_schemes[i].name) == name_len &&
            strncmp(g_schemes[i].name, scheme, name_len) == 0) return true;
    }
    return false;
}

const char* binding_template_validate_params(const char* scheme, size_t name_len, const char* params) {
    for (int i = 0; i < g_scheme_count; i++) {
        if (strlen(g_schemes[i].name) == name_len &&
            strncmp(g_schemes[i].name, scheme, name_len) == 0) {
            return g_schemes[i].validate ? g_schemes[i].validate(params) : nullptr;
        }
    }
    return nullptr;
}

// Find a scheme entry by name. Returns nullptr if not found.
static const SchemeEntry* find_scheme(const char* name, size_t name_len) {
    for (int i = 0; i < g_scheme_count; i++) {
        if (strlen(g_schemes[i].name) == name_len &&
            strncmp(g_schemes[i].name, name, name_len) == 0) {
            return &g_schemes[i];
        }
    }
    return nullptr;
}

// ============================================================================
// Token parsing helper
// ============================================================================

// Find the next binding token starting at *pos in the template.
// A token is [scheme:params] where scheme is alphanumeric.
// On success, sets scheme_start, scheme_len, params_start, params_len,
// and token_end (points past ']'). Returns true if found.
static bool find_next_token(const char* templ, const char** out_token_start,
                            const char** out_scheme, size_t* out_scheme_len,
                            const char** out_params, size_t* out_params_len,
                            const char** out_token_end) {
    const char* p = templ;
    while (*p) {
        if (*p != '[') { p++; continue; }

        const char* token_start = p;
        p++; // skip '['

        // Parse scheme name (alphanumeric + underscore)
        const char* scheme_start = p;
        while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                      (*p >= '0' && *p <= '9') || *p == '_')) {
            p++;
        }
        size_t scheme_len = (size_t)(p - scheme_start);
        if (scheme_len == 0 || *p != ':') {
            // Not a binding token — skip this '['
            p = token_start + 1;
            continue;
        }
        p++; // skip ':'

        // Find closing ']' — params is everything between ':' and ']'
        const char* params_start = p;
        int depth = 1;
        while (*p && depth > 0) {
            if (*p == '[') depth++;
            else if (*p == ']') depth--;
            p++;
        }
        if (depth != 0) {
            // Unclosed bracket — skip
            p = token_start + 1;
            continue;
        }
        // p now points past ']'
        size_t params_len = (size_t)(p - 1 - params_start);

        *out_token_start = token_start;
        *out_scheme = scheme_start;
        *out_scheme_len = scheme_len;
        *out_params = params_start;
        *out_params_len = params_len;
        *out_token_end = p;
        return true;
    }
    return false;
}

// Split pipe fallback from params: "actual_params|fallback"
// Finds the LAST '|' at bracket-depth 0, not inside quotes or nested tokens.
// If found, null-terminates at '|' and returns pointer to fallback string.
static char* split_pipe_fallback(char* buf) {
    char* last_pipe = NULL;
    bool in_quotes = false;
    int bracket_depth = 0;
    for (char* p = buf; *p; p++) {
        if (*p == '"') in_quotes = !in_quotes;
        else if (!in_quotes) {
            if (*p == '[') bracket_depth++;
            else if (*p == ']') { if (bracket_depth > 0) bracket_depth--; }
            else if (*p == '|' && bracket_depth == 0) last_pipe = p;
        }
    }
    if (!last_pipe) return NULL;
    *last_pipe = '\0';
    return last_pipe + 1;
}

// ============================================================================
// Public API
// ============================================================================

bool binding_template_has_bindings(const char* label) {
    if (!label) return false;
    const char *ts, *sc, *pm, *te;
    size_t sl, pl;
    return find_next_token(label, &ts, &sc, &sl, &pm, &pl, &te);
}

bool binding_template_resolve(const char* templ, char* out, size_t out_len) {
    if (!templ || !out || out_len == 0) {
        if (out && out_len > 0) out[0] = '\0';
        return false;
    }

    bool any_resolved = false;
    size_t written = 0;
    const char* p = templ;

    // Null-terminated params buffer for resolver callbacks
    char params_buf[BINDING_TEMPLATE_MAX_LEN];

    while (*p && written < out_len - 1) {
        const char *token_start, *scheme, *params, *token_end;
        size_t scheme_len, params_len;

        if (!find_next_token(p, &token_start, &scheme, &scheme_len,
                             &params, &params_len, &token_end)) {
            // No more tokens — copy remaining static text
            size_t remain = strlen(p);
            size_t space = out_len - 1 - written;
            size_t copy = remain < space ? remain : space;
            memcpy(out + written, p, copy);
            written += copy;
            break;
        }

        // Copy static text before token
        size_t prefix_len = (size_t)(token_start - p);
        if (prefix_len > 0) {
            size_t space = out_len - 1 - written;
            size_t copy = prefix_len < space ? prefix_len : space;
            memcpy(out + written, p, copy);
            written += copy;
        }

        // Resolve the token
        const SchemeEntry* entry = find_scheme(scheme, scheme_len);
        if (!entry || !entry->resolver) {
            // Unknown scheme — emit error
            const char* err = "ERR:unknown";
            size_t elen = strlen(err);
            size_t space = out_len - 1 - written;
            size_t copy = elen < space ? elen : space;
            memcpy(out + written, err, copy);
            written += copy;
        } else {
            // Make null-terminated copy of params
            size_t plen = params_len < sizeof(params_buf) - 1 ? params_len : sizeof(params_buf) - 1;
            memcpy(params_buf, params, plen);
            params_buf[plen] = '\0';

            // Extract pipe fallback before resolving
            char* fallback = split_pipe_fallback(params_buf);

            char resolved[128];
            if (entry->resolver(params_buf, resolved, sizeof(resolved))) {
                any_resolved = true;
                size_t rlen = strlen(resolved);
                size_t space = out_len - 1 - written;
                size_t copy = rlen < space ? rlen : space;
                memcpy(out + written, resolved, copy);
                written += copy;
            } else {
                // Resolver failed — use pipe fallback or placeholder
                const char* ph = fallback ? fallback : "---";
                size_t phlen = strlen(ph);
                size_t space = out_len - 1 - written;
                size_t copy = phlen < space ? phlen : space;
                memcpy(out + written, ph, copy);
                written += copy;
            }
        }

        p = token_end;
    }

    out[written] = '\0';
    return any_resolved;
}

bool binding_template_resolve_single_token(const char* templ, char* out, size_t out_len) {
    if (!templ || !out || out_len == 0) {
        if (out && out_len > 0) out[0] = '\0';
        return false;
    }

    const char *token_start, *scheme, *params, *token_end;
    size_t scheme_len, params_len;
    if (!find_next_token(templ, &token_start, &scheme, &scheme_len,
                         &params, &params_len, &token_end)) {
        out[0] = '\0';
        return false;
    }
    if (token_start != templ || *token_end != '\0') {
        out[0] = '\0';
        return false;
    }

    const SchemeEntry* entry = find_scheme(scheme, scheme_len);
    if (!entry || !entry->resolver) {
        strlcpy(out, "ERR:unknown", out_len);
        return true;
    }

    char params_buf[BINDING_TEMPLATE_MAX_LEN];
    size_t plen = params_len < sizeof(params_buf) - 1 ? params_len : sizeof(params_buf) - 1;
    memcpy(params_buf, params, plen);
    params_buf[plen] = '\0';

    char* fallback = split_pipe_fallback(params_buf);
    if (entry->resolver(params_buf, out, out_len)) {
        return true;
    }

    strlcpy(out, fallback ? fallback : "---", out_len);
    return true;
}

void binding_template_collect_topics(const char* templ, void* user_data) {
    if (!templ) return;

    const char* p = templ;
    char params_buf[BINDING_TEMPLATE_MAX_LEN];

    while (*p) {
        const char *token_start, *scheme, *params, *token_end;
        size_t scheme_len, params_len;

        if (!find_next_token(p, &token_start, &scheme, &scheme_len,
                             &params, &params_len, &token_end)) {
            break;
        }

        const SchemeEntry* entry = find_scheme(scheme, scheme_len);
        if (entry && entry->collector) {
            size_t plen = params_len < sizeof(params_buf) - 1 ? params_len : sizeof(params_buf) - 1;
            memcpy(params_buf, params, plen);
            params_buf[plen] = '\0';
            split_pipe_fallback(params_buf);  // strip fallback before collecting
            entry->collector(params_buf, user_data);
        }

        p = token_end;
    }
}

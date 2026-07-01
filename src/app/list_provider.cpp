#include "list_provider.h"

#if HAS_DISPLAY

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ============================================================================
// ListProvider registry — static array + linear scan
// ============================================================================

static const ListProvider* g_providers[LIST_MAX_PROVIDERS];
static uint8_t g_provider_count = 0;

bool list_provider_register(const ListProvider* provider) {
    if (!provider || g_provider_count >= LIST_MAX_PROVIDERS) return false;
    g_providers[g_provider_count++] = provider;
    return true;
}

const ListProvider* list_provider_find(const char* id) {
    if (!id) return nullptr;
    for (uint8_t i = 0; i < g_provider_count; i++) {
        if (strcmp(g_providers[i]->id, id) == 0) return g_providers[i];
    }
    return nullptr;
}

uint8_t list_provider_count() { return g_provider_count; }

const ListProvider* list_provider_at(uint8_t index) {
    return (index < g_provider_count) ? g_providers[index] : nullptr;
}

// ============================================================================
// Item filter
// ============================================================================

// Case-insensitive glob match: '*' matches any (possibly empty) sequence,
// '?' matches exactly one character. Iterative with backtrack on '*'.
static bool glob_match_ci(const char* pat, size_t pat_len, const char* str) {
    size_t pi = 0, si = 0, slen = strlen(str);
    size_t star_pi = (size_t)-1, star_si = 0;
    while (si < slen) {
        if (pi < pat_len && pat[pi] == '*') {
            star_pi = pi++;
            star_si = si;
        } else if (pi < pat_len && (pat[pi] == '?' ||
                                    tolower((unsigned char)pat[pi]) == tolower((unsigned char)str[si]))) {
            pi++; si++;
        } else if (star_pi != (size_t)-1) {
            pi = star_pi + 1;
            si = ++star_si;
        } else {
            return false;
        }
    }
    while (pi < pat_len && pat[pi] == '*') pi++;
    return pi == pat_len;
}

// Parse "#N" or "#N-M" rule body (without leading '#'). Returns true on success.
static bool parse_index_rule(const char* body, size_t len, int* out_lo, int* out_hi) {
    if (len == 0) return false;
    char buf[16];
    if (len >= sizeof(buf)) return false;
    memcpy(buf, body, len);
    buf[len] = '\0';
    char* dash = strchr(buf, '-');
    if (dash) {
        *dash = '\0';
        char* e1 = nullptr; char* e2 = nullptr;
        long lo = strtol(buf, &e1, 10);
        long hi = strtol(dash + 1, &e2, 10);
        if (e1 == buf || e2 == dash + 1 || *e1 != '\0' || *e2 != '\0') return false;
        if (lo > hi) { long t = lo; lo = hi; hi = t; }
        *out_lo = (int)lo;
        *out_hi = (int)hi;
    } else {
        char* e = nullptr;
        long v = strtol(buf, &e, 10);
        if (e == buf || *e != '\0') return false;
        *out_lo = *out_hi = (int)v;
    }
    return true;
}

// Trim leading/trailing ASCII whitespace by adjusting start pointer and length.
static void trim(const char** s, size_t* len) {
    while (*len > 0 && isspace((unsigned char)**s)) { (*s)++; (*len)--; }
    while (*len > 0 && isspace((unsigned char)(*s)[*len - 1])) { (*len)--; }
}

// Returns true if rule (body without ! prefix) matches the item at index idx.
static bool rule_matches(const char* rule, size_t rule_len, const ListItem* item, uint8_t idx) {
    if (rule_len == 0) return false;
    if (rule[0] == '#') {
        int lo = 0, hi = 0;
        if (!parse_index_rule(rule + 1, rule_len - 1, &lo, &hi)) return false;
        return (int)idx >= lo && (int)idx <= hi;
    }
    // Treat bare "*" as match-all glob too (handled by glob naturally).
    if (glob_match_ci(rule, rule_len, item->id)) return true;
    if (glob_match_ci(rule, rule_len, item->label)) return true;
    return false;
}

// First pass: scan rules to determine if any positive rule exists.
static bool has_positive_rule(const char* filter) {
    const char* p = filter;
    while (*p) {
        const char* start = p;
        while (*p && *p != ',') p++;
        size_t len = (size_t)(p - start);
        trim(&start, &len);
        if (len > 0 && start[0] != '!') return true;
        if (*p == ',') p++;
    }
    return false;
}

uint8_t list_filter_items(ListItem* items, uint8_t count, const char* filter) {
    if (!filter || filter[0] == '\0') return count;
    bool any_positive = has_positive_rule(filter);
    uint8_t write = 0;
    for (uint8_t i = 0; i < count; i++) {
        bool included = !any_positive; // exclusion-only filter starts with all included
        const char* p = filter;
        while (*p) {
            const char* start = p;
            while (*p && *p != ',') p++;
            size_t len = (size_t)(p - start);
            trim(&start, &len);
            if (*p == ',') p++;
            if (len == 0) continue;
            bool exclude = (start[0] == '!');
            const char* body = exclude ? start + 1 : start;
            size_t body_len = exclude ? len - 1 : len;
            trim(&body, &body_len);
            if (body_len == 0) continue;
            if (rule_matches(body, body_len, &items[i], i)) {
                if (exclude) included = false;
                else included = true;
            }
        }
        if (included) {
            if (write != i) items[write] = items[i];
            write++;
        }
    }
    return write;
}

#endif // HAS_DISPLAY

// ============================================================================
// Unit tests for binding template resolver edge cases
// ============================================================================

#include <cstdio>
#include <cstring>

#include "binding_template.h"

static int g_pass = 0;
static int g_fail = 0;

static bool mock_big_resolve(const char* params, char* out, size_t out_len) {
    (void)params;
    // Produce a deterministic long payload to validate truncation behavior.
    for (size_t i = 0; i + 1 < out_len; i++) {
        out[i] = (char)('A' + (i % 26));
    }
    out[out_len - 1] = '\0';
    return true;
}

static bool mock_fail_resolve(const char* params, char* out, size_t out_len) {
    (void)params;
    (void)out;
    (void)out_len;
    return false;
}

static void mock_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

static void check_true(bool cond, const char* label) {
    if (!cond) {
        std::printf("  FAIL [%s]\n", label);
        g_fail++;
        return;
    }
    g_pass++;
}

static void check_eq(const char* got, const char* expected, const char* label) {
    if (std::strcmp(got, expected) != 0) {
        std::printf("  FAIL [%s]\n    got:      \"%s\"\n    expected: \"%s\"\n", label, got, expected);
        g_fail++;
        return;
    }
    g_pass++;
}

static void test_single_token_large_output() {
    std::printf("--- single-token resolver large output ---\n");

    char out_single[512];
    bool single_match = binding_template_resolve_single_token("[big:any]", out_single, sizeof(out_single));
    check_true(single_match, "single token shape detected");
    check_true(std::strlen(out_single) == sizeof(out_single) - 1, "single token preserves large output length");

    char out_generic[512];
    binding_template_resolve("[big:any]", out_generic, sizeof(out_generic));
    check_true(std::strlen(out_generic) == 127, "generic resolver still uses small token buffer");
}

static void test_single_token_shape_rejection() {
    std::printf("--- single-token shape rejection ---\n");
    char out[128];
    bool matched = binding_template_resolve_single_token("Value: [big:any]", out, sizeof(out));
    check_true(!matched, "non-exact token rejected");
    check_eq(out, "", "output cleared for non-match");
}

static void test_single_token_fallback() {
    std::printf("--- single-token fallback ---\n");
    char out[64];
    bool matched = binding_template_resolve_single_token("[fail:key|N/A]", out, sizeof(out));
    check_true(matched, "single token with fallback matched");
    check_eq(out, "N/A", "fallback applied on resolver failure");
}

static void test_single_token_unknown_scheme() {
    std::printf("--- single-token unknown scheme ---\n");
    char out[64];
    bool matched = binding_template_resolve_single_token("[nosuch:key]", out, sizeof(out));
    check_true(matched, "unknown scheme still recognized as single token");
    check_eq(out, "ERR:unknown", "unknown scheme returns error token");
}

int main() {
    std::printf("=== binding_template resolver tests ===\n\n");

    binding_template_register("big", mock_big_resolve, mock_collect);
    binding_template_register("fail", mock_fail_resolve, mock_collect);

    test_single_token_large_output();
    test_single_token_shape_rejection();
    test_single_token_fallback();
    test_single_token_unknown_scheme();

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

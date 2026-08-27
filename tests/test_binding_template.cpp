// ============================================================================
// Unit tests for binding template resolver edge cases
// ============================================================================

#include <cstdio>
#include <cstring>

#include "binding_template.h"

static int g_pass = 0;
static int g_fail = 0;

static BindingResolverStatus mock_big_resolve(const char* params, char* out, size_t out_len) {
    (void)params;
    // Produce a deterministic long payload to validate truncation behavior.
    for (size_t i = 0; i + 1 < out_len; i++) {
        out[i] = (char)('A' + (i % 26));
    }
    out[out_len - 1] = '\0';
    return BINDING_RESOLVER_RESOLVED;
}

static BindingResolverStatus mock_fail_resolve(const char* params, char* out, size_t out_len) {
    (void)params;
    (void)out;
    (void)out_len;
    return BINDING_RESOLVER_UNAVAILABLE;
}

static void mock_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

static uint8_t mock_key_count() { return 1; }
static const char* mock_key_at(uint8_t index) { return index == 0 ? "any" : nullptr; }

static const BindingSchemeSpec kMockFiniteSpec = {
    1, 1, 1, -1, BINDING_VALIDATION_STANDARD, false, mock_key_count, mock_key_at
};

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

static void test_scheme_registry_capacity() {
    std::printf("--- scheme registry capacity ---\n");

    for (uint8_t index = 0; index < BINDING_MAX_SCHEMES - 3; ++index) {
        char scheme_name[16] = {};
        std::snprintf(scheme_name, sizeof(scheme_name), "s%u", (unsigned)index + 1);
        check_true(binding_template_register(scheme_name, mock_big_resolve, mock_collect, kMockFiniteSpec),
                   "scheme accepted within registry capacity");
    }
    check_true(binding_template_scheme_count() == BINDING_MAX_SCHEMES,
               "registry includes all accepted schemes");
    check_true(!binding_template_register("overflow", mock_big_resolve, mock_collect, kMockFiniteSpec),
               "overflow scheme is rejected");
}

static void test_metadata_parameter_validation() {
    std::printf("--- metadata parameter validation ---\n");

    const BindingSchemeSpec expression_spec = {
        1, 2, 1, 1, BINDING_VALIDATION_EXPRESSION, true, nullptr, nullptr
    };
    check_true(binding_template_register("expr", mock_big_resolve, mock_collect, expression_spec),
               "expression scheme registered");
    check_true(binding_template_validate_params("expr", 4, "[big:any;fmt]+1;%.1f") == nullptr,
               "nested parameters do not inflate expression count");
    check_true(binding_template_validate_params("expr", 4, "a;b;c") != nullptr,
               "outer parameter overflow rejected");
    check_true(binding_template_validate_params("big", 3, "any") == nullptr,
               "full finite parameter list accepted");
}

int main() {
    std::printf("=== binding_template resolver tests ===\n\n");

    binding_template_register("big", mock_big_resolve, mock_collect, kMockFiniteSpec);
    binding_template_register("fail", mock_fail_resolve, mock_collect, kMockFiniteSpec);

    test_single_token_large_output();
    test_single_token_shape_rejection();
    test_single_token_fallback();
    test_single_token_unknown_scheme();
    test_metadata_parameter_validation();
    test_scheme_registry_capacity();

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

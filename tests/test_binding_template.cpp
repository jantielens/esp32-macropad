// ============================================================================
// Unit tests for binding template resolver edge cases
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "binding_template.h"
#include <esp_cpu.h>
#include <freertos/task.h>

static int g_pass = 0;
static int g_fail = 0;
static unsigned g_walk_calls = 0;
static unsigned g_walk_fanout = 1;

static BindingResolverStatus mock_walk(const char* params, char* out, size_t out_len) {
    ++g_walk_calls;
    const unsigned depth = static_cast<unsigned>(std::strtoul(params, nullptr, 10));
    if (!depth) {
        std::snprintf(out, out_len, "ok");
        return BINDING_RESOLVER_RESOLVED;
    }
    char next[32];
    std::snprintf(next, sizeof(next), "[walk:%u]", depth - 1);
    for (unsigned i = 0; i < g_walk_fanout; ++i) {
        const auto status = binding_template_resolve_status(next, out, out_len);
        if (status != BINDING_RESOLVER_RESOLVED) return status;
    }
    return BINDING_RESOLVER_RESOLVED;
}

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

static BindingResolverStatus mock_nested_resolve(const char*, char* out, size_t out_len) {
    char nested[64];
    binding_template_resolve("[big:any]", nested, sizeof(nested));
    std::snprintf(out, out_len, "%s", nested);
    return BINDING_RESOLVER_RESOLVED;
}

static BindingResolverStatus mock_utf8_resolve(const char*, char* out, size_t out_len) {
    std::snprintf(out, out_len, "A\xe2\x82\xacZ");
    return BINDING_RESOLVER_RESOLVED;
}

static BindingResolverStatus mock_nested_fail(const char*, char* out, size_t out_len) {
    return binding_template_resolve_status("[fail:any]", out, out_len);
}

static void mock_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

static uint8_t mock_key_count() { return 1; }
static const char* mock_key_at(uint8_t index) { return index == 0 ? "any" : nullptr; }

static const BindingSchemeSpec kMockFiniteSpec = {
    1, 1, 1, -1, BINDING_VALIDATION_STANDARD, false, mock_key_count, mock_key_at, nullptr
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

    BindingTokenView token = {};
    check_true(binding_template_single_token("[voice_note:recent_notes]", &token),
               "token view accepts exact token");
    check_true(std::string(token.scheme, token.scheme_len) == "voice_note" &&
               std::string(token.params, token.params_len) == "recent_notes",
               "token view exposes exact scheme and parameter spans");
    check_true(!binding_template_single_token("[big:any]suffix", &token),
               "token view rejects suffix");
    check_true(!binding_template_single_token("prefix[big:any]", &token),
               "token view rejects prefix");
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

    const uint8_t remaining = BINDING_MAX_SCHEMES - binding_template_scheme_count();
    for (uint8_t index = 0; index < remaining; ++index) {
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
        1, 2, 1, 1, BINDING_VALIDATION_EXPRESSION, true, nullptr, nullptr, nullptr
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

static void test_nested_token_visit() {
    std::vector<std::string> tokens;
    binding_template_visit_tokens(
        "[expr:[ha:sensor.power]+[ha:light.kitchen;attributes.brightness]|0] [pad:power]",
        [](const char* scheme, size_t size, const char* params, void* user) {
            static_cast<std::vector<std::string>*>(user)->push_back(
                std::string(scheme, size) + ":" + params);
        }, &tokens);
    check_true(tokens.size() == 4, "visitor discovers nested expression dependencies");
    if (tokens.size() != 4) return;
    check_eq(tokens[0].c_str(), "expr:[ha:sensor.power]+[ha:light.kitchen;attributes.brightness]",
             "outer fallback removed before visiting");
    check_eq(tokens[1].c_str(), "ha:sensor.power", "first nested HA dependency");
    check_eq(tokens[2].c_str(), "ha:light.kitchen;attributes.brightness", "attribute dependency intact");
    check_eq(tokens[3].c_str(), "pad:power", "named dependency available for scoped expansion");
}

static void test_stack_headroom_guard() {
    const uintptr_t previous = test_esp_stack_pointer;
    const uintptr_t base = reinterpret_cast<uintptr_t>(test_freertos_stack_start);
    char out[64];
    test_esp_stack_pointer = base + BINDING_RESOLVER_STACK_RESERVE - 1;
    g_walk_calls = 0;
    check_true(binding_template_resolve_status("[walk:0]", out, sizeof(out)) ==
                   BINDING_RESOLVER_UNAVAILABLE && !g_walk_calls,
               "insufficient current stack rejects before entering the callback");
    check_true(binding_template_resolve_checked("[walk:0|safe]", out, sizeof(out)),
               "low stack still permits an explicit checked fallback");
    check_eq(out, "safe", "stack exhaustion uses the requested fallback");
    test_esp_stack_pointer = base - 1;
    check_true(!binding_template_has_stack_headroom(1), "stack bounds do not underflow");
    test_esp_stack_pointer = base + BINDING_RESOLVER_STACK_RESERVE;
    check_true(binding_template_resolve_status("[walk:0]", out, sizeof(out)) ==
                   BINDING_RESOLVER_RESOLVED && g_walk_calls == 1,
               "exact stack reserve permits evaluation and resets after failure");
    test_esp_stack_pointer = previous;
}

int main() {
    std::printf("=== binding_template resolver tests ===\n\n");

    binding_template_register("big", mock_big_resolve, mock_collect, kMockFiniteSpec);
    binding_template_register("fail", mock_fail_resolve, mock_collect, kMockFiniteSpec);
    binding_template_register("nested", mock_nested_resolve, mock_collect, kMockFiniteSpec);
    binding_template_register("utf8", mock_utf8_resolve, mock_collect, kMockFiniteSpec);
    binding_template_register("nestedfail", mock_nested_fail, mock_collect, kMockFiniteSpec);
    binding_template_register("walk", mock_walk, nullptr,
                              {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, true});

    char bounded[64];
    binding_template_resolve("[walk:9]", bounded, sizeof(bounded));
    check_eq(bounded, "ok", "ten active resolver callbacks still succeed");
    g_walk_calls = 0;
    check_true(binding_template_resolve_status("[walk:10]", bounded, sizeof(bounded)) ==
                   BINDING_RESOLVER_UNAVAILABLE,
               "arbitrary scheme re-entry is depth bounded without pad or expr tokens");
    check_true(g_walk_calls <= BINDING_MAX_RESOLVER_DEPTH, "callback depth guard bounds stack growth");
    g_walk_calls = 0;
    g_walk_fanout = 4;
    check_true(binding_template_resolve_status("[walk:6]", bounded, sizeof(bounded)) ==
                   BINDING_RESOLVER_UNAVAILABLE,
               "arbitrary scheme fanout is work bounded");
    check_true(g_walk_calls <= BINDING_MAX_RESOLVER_EVALUATIONS, "callback budget bounds recursive work");
    binding_template_resolve("[walk:0]", bounded, sizeof(bounded));
    check_eq(bounded, "ok", "resolver guards reset after a rejected lookup");

    char checked[256];
    check_true(binding_template_resolve_checked("https://host/image.jpg", checked, sizeof(checked)),
               "checked literal source is complete");
    check_true(!binding_template_resolve_checked("[big:any]", checked, sizeof(checked)),
               "checked source rejects saturated resolver output");
    check_true(!binding_template_resolve_checked("https://host/[nested:any]", checked, sizeof(checked)),
               "checked source rejects truncation inside nested resolution");
    check_true(binding_template_resolve_checked("https://host/[fail:any|fallback]", checked, sizeof(checked)) &&
                   std::strcmp(checked, "https://host/fallback") == 0,
               "checked source accepts an explicit fallback for an unavailable binding");
    check_true(binding_template_resolve_checked("https://host/restored.jpg", checked, sizeof(checked)),
               "checked failure does not leak into the next resolution");
    check_true(binding_template_resolve_checked("[nestedfail:any|safe]", checked, sizeof(checked)) &&
                   std::strcmp(checked, "safe") == 0,
               "outer fallback satisfies a failed nested checked resolution");
    char clipped[5];
    binding_template_resolve("X[utf8:any]", clipped, sizeof(clipped));
    check_eq(clipped, "XA", "composed preview does not split a multibyte character");

    test_single_token_large_output();
    test_single_token_shape_rejection();
    test_single_token_fallback();
    test_single_token_unknown_scheme();
    test_metadata_parameter_validation();
    test_nested_token_visit();
    test_stack_headroom_guard();
    test_scheme_registry_capacity();

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

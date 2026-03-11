// ============================================================================
// Integration tests for pad binding scheme — [pad:name] and [pad:name;fmt]
// ============================================================================
// Host-native test (no ESP32 needed). Exercises:
//   - Basic [pad:name] resolution through mock MQTT/health
//   - Per-usage format override [pad:name;fmt]
//   - Nested inside [expr:...]
//   - Error cases (unknown name, no page context, recursive)
//   - pad_binding_expand() text substitution for data streams
//   - Topic collection through [pad:] bindings

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>

#include "binding_template.h"
#include "pad_binding.h"
#include "expr_eval.h"

// ============================================================================
// Mock MQTT resolver
// ============================================================================

static std::map<std::string, std::string> g_mqtt_values;

static bool mock_mqtt_resolve(const char* params, char* out, size_t out_len) {
    if (!params) return false;
    // Parse "topic;path;format" — key is "topic;path"
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", params);
    // Find last ';' for format
    char* last_semi = nullptr;
    int semi_count = 0;
    for (char* p = buf; *p; p++) {
        if (*p == ';') { last_semi = p; semi_count++; }
    }
    // Build lookup key (topic;path — first two segments)
    std::string key;
    if (semi_count >= 2 && last_semi) {
        *last_semi = '\0';
        key = buf;
    } else {
        key = buf;
    }
    auto it = g_mqtt_values.find(key);
    if (it == g_mqtt_values.end()) return false;
    snprintf(out, out_len, "%s", it->second.c_str());
    return true;
}

static void mock_mqtt_collect(const char* params, void* user_data) {
    (void)params; (void)user_data;
}

// ============================================================================
// Mock health resolver
// ============================================================================

static std::map<std::string, std::string> g_health_values;

static bool mock_health_resolve(const char* params, char* out, size_t out_len) {
    if (!params) return false;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", params);
    char* semi = strchr(buf, ';');
    if (semi) *semi = '\0';
    auto it = g_health_values.find(buf);
    if (it == g_health_values.end()) return false;
    snprintf(out, out_len, "%s", it->second.c_str());
    return true;
}

static void mock_health_collect(const char* params, void* user_data) {
    (void)params; (void)user_data;
}

// ============================================================================
// expr resolver (same as test_expr_binding.cpp)
// ============================================================================

static char* split_format(char* buf) {
    char* last_sep = NULL;
    bool in_quotes = false;
    int bracket_depth = 0;
    for (char* p = buf; *p; p++) {
        if (*p == '"') in_quotes = !in_quotes;
        else if (!in_quotes) {
            if (*p == '[') bracket_depth++;
            else if (*p == ']') { if (bracket_depth > 0) bracket_depth--; }
            else if (*p == ';' && bracket_depth == 0) last_sep = p;
        }
    }
    if (!last_sep) return NULL;
    *last_sep = '\0';
    char* fmt = last_sep + 1;
    while (*fmt == ' ' || *fmt == '\t') fmt++;
    return fmt[0] ? fmt : NULL;
}

static bool expr_test_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        snprintf(out, out_len, "ERR:empty expr");
        return false;
    }
    char buf[BINDING_TEMPLATE_MAX_LEN];
    snprintf(buf, sizeof(buf), "%s", params);

    char* fmt = split_format(buf);

    char resolved[BINDING_TEMPLATE_MAX_LEN];
    binding_template_resolve(buf, resolved, sizeof(resolved));

    if (strstr(resolved, "---") != NULL) {
        snprintf(out, out_len, "---");
        return false;
    }

    char eval_result[EXPR_STR_MAX];
    if (!expr_eval(resolved, eval_result, sizeof(eval_result))) {
        snprintf(out, out_len, "%s", eval_result);
        return false;
    }

    if (fmt) {
        char* end = NULL;
        double val = strtod(eval_result, &end);
        if (end && end != eval_result && *end == '\0') {
            snprintf(out, out_len, fmt, val);
        } else {
            snprintf(out, out_len, "%s", eval_result);
        }
    } else {
        snprintf(out, out_len, "%s", eval_result);
    }
    return true;
}

static void expr_test_collect(const char* params, void* user_data) {
    if (!params) return;
    char buf[BINDING_TEMPLATE_MAX_LEN];
    snprintf(buf, sizeof(buf), "%s", params);
    split_format(buf);
    binding_template_collect_topics(buf, user_data);
}

// ============================================================================
// Test helpers
// ============================================================================

static int g_pass = 0;
static int g_fail = 0;

static void check(const char* templ, const char* expected, const char* label) {
    char out[BINDING_TEMPLATE_MAX_LEN];
    binding_template_resolve(templ, out, sizeof(out));
    if (strcmp(out, expected) != 0) {
        printf("  FAIL [%s]: resolve(\"%s\")\n        got:      \"%s\"\n        expected: \"%s\"\n",
               label, templ, out, expected);
        g_fail++;
    } else {
        g_pass++;
    }
}

// Helper to build a PadPageConfig with bindings
static PadPageConfig make_page(std::initializer_list<std::pair<const char*, const char*>> bindings) {
    PadPageConfig page = {};
    uint8_t i = 0;
    for (auto& [name, value] : bindings) {
        if (i >= PAD_MAX_BINDINGS) break;
        strlcpy(page.bindings[i].name, name, PAD_BINDING_NAME_MAX_LEN);
        strlcpy(page.bindings[i].value, value, CONFIG_LABEL_MAX_LEN);
        i++;
    }
    page.binding_count = i;
    return page;
}

// ============================================================================
// Test groups
// ============================================================================

static void test_basic_resolution() {
    printf("--- Basic [pad:name] resolution ---\n");
    g_mqtt_values["solar/power;$.value"] = "3200";
    g_mqtt_values["grid/power;$.watts"] = "1100";

    auto page = make_page({
        {"power",      "[mqtt:solar/power;$.value]"},
        {"grid_power", "[mqtt:grid/power;$.watts]"},
    });
    pad_binding_set_page(&page);

    check("[pad:power]", "3200", "simple pad ref");
    check("[pad:grid_power]", "1100", "underscore name");

    pad_binding_set_page(nullptr);
}

static void test_format_override() {
    printf("--- Per-usage format override ---\n");
    g_mqtt_values["solar/power;$.value"] = "3200.567";

    auto page = make_page({
        {"power", "[mqtt:solar/power;$.value]"},
    });
    pad_binding_set_page(&page);

    check("[pad:power;%.1f]", "3200.6", "one decimal");
    check("[pad:power;%.0f]", "3201", "zero decimals");
    check("[pad:power;%.2f kW]", "3200.57 kW", "format with suffix");

    pad_binding_set_page(nullptr);
}

static void test_with_static_text() {
    printf("--- Mixed static text + [pad:] ---\n");
    g_mqtt_values["solar/power;$.value"] = "3200";

    auto page = make_page({
        {"power", "[mqtt:solar/power;$.value]"},
    });
    pad_binding_set_page(&page);

    check("Solar: [pad:power] W", "Solar: 3200 W", "prefix and suffix");
    check("[pad:power]W", "3200W", "tight suffix");

    pad_binding_set_page(nullptr);
}

static void test_inside_expr() {
    printf("--- [pad:] inside [expr:] ---\n");
    g_mqtt_values["solar/power;$.value"] = "3200";
    g_mqtt_values["grid/power;$.watts"] = "1100";

    auto page = make_page({
        {"power",      "[mqtt:solar/power;$.value]"},
        {"grid_power", "[mqtt:grid/power;$.watts]"},
    });
    pad_binding_set_page(&page);

    check("[expr:[pad:power] / 1000]", "3.2", "pad in expr division");
    check("[expr:[pad:power] - [pad:grid_power]]", "2100", "two pads in expr");
    check("[expr:[pad:power] > 3000 ? \"High\" : \"Low\"]", "High", "pad in ternary");

    g_mqtt_values["solar/power;$.value"] = "500";
    check("[expr:[pad:power] > 3000 ? \"High\" : \"Low\"]", "Low", "pad ternary low");

    pad_binding_set_page(nullptr);
}

static void test_expr_format_with_pad() {
    printf("--- [expr:] with format and [pad:] ---\n");
    g_mqtt_values["solar/power;$.value"] = "3200";

    auto page = make_page({
        {"power", "[mqtt:solar/power;$.value]"},
    });
    pad_binding_set_page(&page);

    check("[expr:[pad:power] / 1000;%.2f]", "3.20", "expr format with pad");

    pad_binding_set_page(nullptr);
}

static void test_unknown_binding_name() {
    printf("--- Unknown binding name ---\n");
    auto page = make_page({
        {"power", "[mqtt:solar/power;$.value]"},
    });
    pad_binding_set_page(&page);

    check("[pad:nonexistent]", "---", "unknown name");
    check("Value: [pad:nope]", "Value: ---", "unknown with prefix");

    pad_binding_set_page(nullptr);
}

static void test_no_page_context() {
    printf("--- No page context set ---\n");
    // Ensure context is cleared
    pad_binding_set_page(nullptr);

    check("[pad:power]", "---", "no page context");
}

static void test_empty_params() {
    printf("--- Empty params ---\n");
    auto page = make_page({{"power", "[mqtt:solar/power;$.value]"}});
    pad_binding_set_page(&page);

    check("[pad:]", "---", "empty name");

    pad_binding_set_page(nullptr);
}

static void test_recursive_guard() {
    printf("--- Recursive [pad:] guard ---\n");
    auto page = make_page({
        {"a", "[pad:b]"},        // references another pad binding
        {"b", "[mqtt:x;$.v]"},
    });
    pad_binding_set_page(&page);

    check("[pad:a]", "---", "recursive pad ref");
    // Non-recursive should still work
    g_mqtt_values["x;$.v"] = "42";
    check("[pad:b]", "42", "non-recursive works");

    pad_binding_set_page(nullptr);
}

static void test_set_bindings_api() {
    printf("--- set_bindings lightweight API ---\n");
    g_mqtt_values["solar/power;$.value"] = "5000";

    PadBinding bindings[2];
    strlcpy(bindings[0].name, "power", PAD_BINDING_NAME_MAX_LEN);
    strlcpy(bindings[0].value, "[mqtt:solar/power;$.value]", CONFIG_LABEL_MAX_LEN);
    strlcpy(bindings[1].name, "temp", PAD_BINDING_NAME_MAX_LEN);
    strlcpy(bindings[1].value, "25.5", CONFIG_LABEL_MAX_LEN);

    pad_binding_set_bindings(bindings, 2);

    check("[pad:power]", "5000", "set_bindings: mqtt");
    check("[pad:temp]", "25.5", "set_bindings: static value");

    pad_binding_set_bindings(nullptr, 0);
    check("[pad:power]", "---", "set_bindings: cleared");
}

static void test_multiple_pad_tokens() {
    printf("--- Multiple [pad:] tokens in one label ---\n");
    g_mqtt_values["solar/power;$.value"] = "3200";
    g_mqtt_values["grid/power;$.watts"] = "1100";

    auto page = make_page({
        {"solar", "[mqtt:solar/power;$.value]"},
        {"grid",  "[mqtt:grid/power;$.watts]"},
    });
    pad_binding_set_page(&page);

    check("[pad:solar]W / [pad:grid]W", "3200W / 1100W", "two pad tokens");

    pad_binding_set_page(nullptr);
}

static void test_health_in_pad_binding() {
    printf("--- Health binding via [pad:] ---\n");
    g_health_values["heap_free"] = "187392";

    auto page = make_page({
        {"heap", "[health:heap_free]"},
    });
    pad_binding_set_page(&page);

    check("[pad:heap]", "187392", "health through pad");
    check("[expr:[pad:heap] / 1024]", "183", "health through pad in expr");

    pad_binding_set_page(nullptr);
}

// ============================================================================
// pad_binding_expand() tests (text substitution for data streams)
// ============================================================================

static void test_expand_basic() {
    printf("--- pad_binding_expand: basic ---\n");
    auto page = make_page({
        {"power", "[mqtt:solar/power;$.value]"},
        {"grid",  "[mqtt:grid/power;$.watts]"},
    });

    char out[256];
    bool expanded;

    expanded = pad_binding_expand(&page, "[pad:power]", out, sizeof(out));
    if (!expanded || strcmp(out, "[mqtt:solar/power;$.value]") != 0) {
        printf("  FAIL [expand basic]: got \"%s\" (expanded=%d)\n", out, expanded);
        g_fail++;
    } else {
        g_pass++;
    }

    expanded = pad_binding_expand(&page, "Prefix [pad:grid] suffix", out, sizeof(out));
    if (!expanded || strcmp(out, "Prefix [mqtt:grid/power;$.watts] suffix") != 0) {
        printf("  FAIL [expand with text]: got \"%s\"\n", out);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_expand_no_pad_tokens() {
    printf("--- pad_binding_expand: no pad tokens ---\n");
    auto page = make_page({{"power", "[mqtt:solar/power;$.value]"}});

    char out[256];
    bool expanded = pad_binding_expand(&page, "[mqtt:solar/power;$.value]", out, sizeof(out));
    if (expanded || strcmp(out, "[mqtt:solar/power;$.value]") != 0) {
        printf("  FAIL [expand no tokens]: got \"%s\" (expanded=%d)\n", out, expanded);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_expand_unknown_binding() {
    printf("--- pad_binding_expand: unknown binding ---\n");
    auto page = make_page({{"power", "[mqtt:solar/power;$.value]"}});

    char out[256];
    bool expanded = pad_binding_expand(&page, "[pad:unknown]", out, sizeof(out));
    // Unknown binding stays as-is
    if (expanded || strcmp(out, "[pad:unknown]") != 0) {
        printf("  FAIL [expand unknown]: got \"%s\" (expanded=%d)\n", out, expanded);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_expand_with_format() {
    printf("--- pad_binding_expand: with format suffix ---\n");
    auto page = make_page({{"power", "[mqtt:solar/power;$.value]"}});

    char out[256];
    // [pad:power;%.2f] has format — should NOT be expanded (kept for runtime)
    bool expanded = pad_binding_expand(&page, "[pad:power;%.2f]", out, sizeof(out));
    if (expanded || strcmp(out, "[pad:power;%.2f]") != 0) {
        printf("  FAIL [expand with format]: got \"%s\" (expanded=%d)\n", out, expanded);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_expand_recursive_guard() {
    printf("--- pad_binding_expand: recursive guard ---\n");
    auto page = make_page({
        {"a", "[pad:b]"},       // recursive — references another pad
        {"b", "[mqtt:x;$.v]"},
    });

    char out[256];
    // [pad:a] whose value contains [pad:b] — should stay as-is
    bool expanded = pad_binding_expand(&page, "[pad:a]", out, sizeof(out));
    if (expanded || strcmp(out, "[pad:a]") != 0) {
        printf("  FAIL [expand recursive]: got \"%s\" (expanded=%d)\n", out, expanded);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_expand_multiple_tokens() {
    printf("--- pad_binding_expand: multiple tokens ---\n");
    auto page = make_page({
        {"solar", "[mqtt:solar/power;$.value]"},
        {"grid",  "[mqtt:grid/power;$.watts]"},
    });

    char out[512];
    bool expanded = pad_binding_expand(&page,
        "[expr:[pad:solar] - [pad:grid]]",
        out, sizeof(out));
    if (!expanded || strcmp(out, "[expr:[mqtt:solar/power;$.value] - [mqtt:grid/power;$.watts]]") != 0) {
        printf("  FAIL [expand multiple]: got \"%s\"\n", out);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_expand_null_page() {
    printf("--- pad_binding_expand: null page ---\n");
    char out[256];
    bool expanded = pad_binding_expand(nullptr, "[pad:power]", out, sizeof(out));
    // Null page → copy as-is, return false
    if (expanded || strcmp(out, "[pad:power]") != 0) {
        printf("  FAIL [expand null page]: got \"%s\" (expanded=%d)\n", out, expanded);
        g_fail++;
    } else {
        g_pass++;
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== pad binding integration tests ===\n\n");

    // Register schemes (order: inner before outer)
    binding_template_register("mqtt",   mock_mqtt_resolve,   mock_mqtt_collect);
    binding_template_register("health", mock_health_resolve, mock_health_collect);
    binding_template_register("expr",   expr_test_resolve,   expr_test_collect);
    pad_binding_init();  // registers "pad" scheme

    // Resolution tests
    test_basic_resolution();
    test_format_override();
    test_with_static_text();
    test_inside_expr();
    test_expr_format_with_pad();
    test_unknown_binding_name();
    test_no_page_context();
    test_empty_params();
    test_recursive_guard();
    test_set_bindings_api();
    test_multiple_pad_tokens();
    test_health_in_pad_binding();

    // Expand tests
    test_expand_basic();
    test_expand_no_pad_tokens();
    test_expand_unknown_binding();
    test_expand_with_format();
    test_expand_recursive_guard();
    test_expand_multiple_tokens();
    test_expand_null_page();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

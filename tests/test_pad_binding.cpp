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
#include <vector>

#include "binding_template.h"
#include "pad_binding.h"
#include "expr_eval.h"

// ============================================================================
// Mock MQTT resolver
// ============================================================================

static std::map<std::string, std::string> g_mqtt_values;
static std::vector<std::string> g_collected_topics;
static std::vector<std::string> g_subscription_checks;

extern "C" void mqtt_sub_store_ensure_binding_subscribed(const char* binding_template) {
    g_subscription_checks.emplace_back(binding_template ? binding_template : "");
}

static BindingResolverStatus mock_mqtt_resolve(const char* params, char* out, size_t out_len) {
    if (!params) return BINDING_RESOLVER_UNAVAILABLE;
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
    if (it == g_mqtt_values.end()) return BINDING_RESOLVER_UNAVAILABLE;
    snprintf(out, out_len, "%s", it->second.c_str());
    return BINDING_RESOLVER_RESOLVED;
}

static void mock_mqtt_collect(const char* params, void* user_data) {
    (void)user_data;
    if (!params) return;
    const char* separator = strchr(params, ';');
    g_collected_topics.emplace_back(params, separator ? (size_t)(separator - params) : strlen(params));
}

// ============================================================================
// Mock health resolver
// ============================================================================

static std::map<std::string, std::string> g_health_values;

static BindingResolverStatus mock_health_resolve(const char* params, char* out, size_t out_len) {
    if (!params) return BINDING_RESOLVER_UNAVAILABLE;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", params);
    char* semi = strchr(buf, ';');
    if (semi) *semi = '\0';
    auto it = g_health_values.find(buf);
    if (it == g_health_values.end()) return BINDING_RESOLVER_UNAVAILABLE;
    snprintf(out, out_len, "%s", it->second.c_str());
    return BINDING_RESOLVER_RESOLVED;
}

static BindingResolverStatus mock_loaded_resolve(const char* params, char* out, size_t out_len) {
    const std::string value = strcmp(params, "long") == 0 ? std::string(220, 'x') : "[item:id]";
    if (binding_template_checked_resolution_active() && value.size() >= out_len)
        return BINDING_RESOLVER_UNAVAILABLE;
    snprintf(out, out_len, "%s", value.c_str());
    return BINDING_RESOLVER_RESOLVED;
}

static void mock_health_collect(const char* params, void* user_data) {
    (void)params; (void)user_data;
}

// ============================================================================
// expr resolver (same as test_expr_binding.cpp)
// ============================================================================

static unsigned g_expression_evaluations = 0;

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

static BindingResolverStatus expr_test_resolve(const char* params, char* out, size_t out_len) {
    ++g_expression_evaluations;
    if (!params || !params[0]) {
        snprintf(out, out_len, "ERR:empty expr");
        return BINDING_RESOLVER_UNAVAILABLE;
    }
    char buf[BINDING_TEMPLATE_MAX_LEN];
    snprintf(buf, sizeof(buf), "%s", params);

    char* fmt = split_format(buf);

    char resolved[BINDING_TEMPLATE_MAX_LEN];
    binding_template_resolve(buf, resolved, sizeof(resolved));

    if (strstr(resolved, "---") != NULL) {
        snprintf(out, out_len, "---");
        return BINDING_RESOLVER_UNAVAILABLE;
    }

    char eval_result[EXPR_STR_MAX];
    if (!expr_eval(resolved, eval_result, sizeof(eval_result))) {
        snprintf(out, out_len, "%s", eval_result);
        return BINDING_RESOLVER_UNAVAILABLE;
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
    return BINDING_RESOLVER_RESOLVED;
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

// Helper to build a PadConfig with bindings
static PadConfig make_page(std::initializer_list<std::pair<const char*, const char*>> bindings) {
    PadConfig page = {};
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

static void test_checked_loaded_alias() {
    auto page = make_page({{"short", "[loaded:short]"}, {"long", "[loaded:long]"}});
    pad_binding_set_page(&page);
    char out[256];
    if (!binding_template_resolve_checked("[pad:short]", out, sizeof(out)) ||
        strcmp(out, "[item:id]") != 0) {
        printf("  FAIL [checked alias preserves literal binding-like text]\n");
        ++g_fail;
    } else ++g_pass;
    if (binding_template_resolve_checked("[pad:long]", out, sizeof(out))) {
        printf("  FAIL [checked alias rejects oversized source]\n");
        ++g_fail;
    } else ++g_pass;
    if (!binding_template_resolve_checked("[pad:long|fallback]", out, sizeof(out)) ||
        strcmp(out, "fallback") != 0) {
        printf("  FAIL [checked alias uses explicit fallback]\n");
        ++g_fail;
    } else ++g_pass;
    pad_binding_set_page(nullptr);
}

// Direct string compare (for pad_resolve outputs, which are not re-resolved).
static void check_str(const char* got, const char* expected, const char* label) {
    if (strcmp(got, expected) != 0) {
        printf("  FAIL [%s]:\n        got:      \"%s\"\n        expected: \"%s\"\n",
               label, got, expected);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void check_true(bool value, const char* label) {
    if (value) ++g_pass;
    else {
        printf("  FAIL [%s]\n", label);
        ++g_fail;
    }
}

static void test_composition_limits() {
    auto page = make_page({
        {"a", "[pad:b]"}, {"b", "[mqtt:bounded;value]"}, {"too_deep", "[pad:a]"},
        {"missing", "[pad:unknown]"},
    });
    g_mqtt_values["bounded;value"] = "21";
    pad_binding_set_page(&page);
    check("[pad:a]", "21", "two names per chain work");
    check("[pad:too_deep]", "---", "third name is unavailable");
    check("[pad:too_deep|limit]", "limit", "excessive depth uses fallback");
    check("[pad:missing|absent]", "absent", "missing dependency uses fallback");
    char out[256];
    check_true(!binding_template_resolve_checked("[pad:too_deep]", out, sizeof(out)),
               "checked resolution rejects excessive depth");
    check_true(pad_binding_expand(&page, "[pad:a]", out, sizeof(out)) == PadBindingExpandStatus::EXPANDED,
               "history expands exactly two names");
    check_str(out, "[mqtt:bounded;value]", "history expands composed sources");
    check_true(pad_binding_expand(&page, "[pad:too_deep]", out, sizeof(out)) == PadBindingExpandStatus::UNAVAILABLE,
               "history rejects third name");
    g_collected_topics.clear();
    binding_template_collect_topics("[pad:a] [pad:too_deep]", nullptr);
    check_true(g_collected_topics == std::vector<std::string>{"bounded"},
               "topic collection uses the same depth limit");
    pad_binding_set_page(nullptr);
}

static void test_shared_dependencies_and_work_limit() {
    auto page = make_page({
        {"sum", "[expr:[pad:source]+[pad:source]]"},
        {"source", "[mqtt:shared;value]"},
    });
    g_mqtt_values["shared;value"] = "2";
    pad_binding_set_page(&page);
    check("[pad:sum]", "4", "shared dependency is not a cycle");
    check("[pad:source] [pad:source]", "2 2", "repeated references reset depth");
    g_collected_topics.clear();
    binding_template_collect_topics("[pad:sum]", nullptr);
    check_true(g_collected_topics == std::vector<std::string>({"shared", "shared"}),
               "collector follows both shared dependencies");
    char out[512];
    check_true(pad_binding_expand(&page, "[pad:sum]", out, sizeof(out)) == PadBindingExpandStatus::EXPANDED,
               "shared dependencies expand");
    check_str(out, "[expr:[mqtt:shared;value]+[mqtt:shared;value]]", "history expansion preserves expression");
    check_true(pad_binding_expand(&page, "[pad:sum]", out, 16) == PadBindingExpandStatus::UNAVAILABLE,
               "oversized expansion is not registered as partial data");
    check_str(out, "[pad:sum]", "failed expansion restores original");

    g_collected_topics.clear();
    pad_binding_visit_dependencies("sum", +[](const char*, void*) {
        for (unsigned i = 0; i < PAD_BINDING_MAX_EVALUATIONS; ++i)
            binding_template_collect_topics("[pad:source]", nullptr);
    }, nullptr);
    check_true(g_collected_topics.size() == PAD_BINDING_MAX_EVALUATIONS - 1,
               "collector bounds total work, not only recursion depth");
    check("[pad:source]", "2", "work budget resets for the next lookup");
    pad_binding_set_page(nullptr);
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
    printf("--- Composed [pad:] and runtime guard ---\n");
    auto page = make_page({
        {"a", "[pad:b]"},
        {"b", "[mqtt:x;$.v]"},
        {"cycle", "[pad:other]"},
        {"other", "[pad:cycle]"},
        {"self", "[pad:self]"},
        {"tempg", "[expr:[pad:tempg]+1]"},
        {"mutual_a", "[expr:[pad:mutual_b]+1]"},
        {"mutual_b", "[expr:[pad:mutual_a]+1]"},
        {"defaulted", "[expr:[pad:defaulted|0]+1]"},
        {"source_cycle", "[expr:[pad:source_cycle]+[mqtt:cycle/tail;$.v]]"},
    });
    pad_binding_set_page(&page);

    g_mqtt_values["x;$.v"] = "42";
    check("[pad:a]", "42", "composed pad ref");
    check("[pad:cycle]", "---", "indirect cycle is unavailable");
    check("[pad:self|cycle]", "cycle", "direct cycle uses explicit fallback");
    check("[pad:cycle] [pad:a]", "--- 42", "guard unwinds after failure");
    check("[pad:a;%.1f W]", "42.0 W", "format on composed value");
    check("[pad:b]", "42", "non-recursive works");
    g_expression_evaluations = 0;
    check("[pad:tempg|cycle]", "cycle", "reported self-referencing expression uses fallback");
    check_true(g_expression_evaluations == 1, "self-cycle never re-enters its expression");
    g_expression_evaluations = 0;
    check("[pad:mutual_a|cycle]", "cycle", "mutual expression cycle uses fallback");
    check_true(g_expression_evaluations == 2, "mutual cycle never re-enters an active expression");
    check("[pad:defaulted]", "1", "cycle fallback is used at the first repeated name");
    check("[pad:a]", "42", "valid lookup succeeds after expression cycles");
    g_collected_topics.clear();
    binding_template_collect_topics("[pad:source_cycle]", nullptr);
    check_true(g_collected_topics == std::vector<std::string>{"cycle/tail"},
               "cycle collection visits the non-cyclic source only once");

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

static void test_pad_topic_collection() {
    printf("--- MQTT topic collection via [pad:] ---\n");
    auto page = make_page({
        {"power", "[mqtt:solar/power;$.value]"},
    });
    pad_binding_set_page(&page);
    g_collected_topics.clear();

    binding_template_collect_topics("[pad:power]", nullptr);

    if (g_collected_topics.size() != 1 || g_collected_topics[0] != "solar/power") {
        printf("  FAIL [pad topic collection]: expected solar/power, got %zu topic(s)\n",
               g_collected_topics.size());
        g_fail++;
    } else {
        g_pass++;
    }
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
    PadBindingExpandStatus expanded;

    expanded = pad_binding_expand(&page, "[pad:power]", out, sizeof(out));
    if (expanded != PadBindingExpandStatus::EXPANDED || strcmp(out, "[mqtt:solar/power;$.value]") != 0) {
        printf("  FAIL [expand basic]: got \"%s\" (expanded=%d)\n", out, static_cast<int>(expanded));
        g_fail++;
    } else {
        g_pass++;
    }

    expanded = pad_binding_expand_bindings(page.bindings, page.binding_count,
                                           "[pad:power]", out, sizeof(out));
    if (expanded != PadBindingExpandStatus::EXPANDED || strcmp(out, "[mqtt:solar/power;$.value]") != 0) {
        printf("  FAIL [expand explicit bindings]: got \"%s\" (expanded=%d)\n", out, static_cast<int>(expanded));
        g_fail++;
    } else {
        g_pass++;
    }

    expanded = pad_binding_expand(&page, "Prefix [pad:grid] suffix", out, sizeof(out));
    if (expanded != PadBindingExpandStatus::EXPANDED || strcmp(out, "Prefix [mqtt:grid/power;$.watts] suffix") != 0) {
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
    auto expanded = pad_binding_expand(&page, "[mqtt:solar/power;$.value]", out, sizeof(out));
    if (expanded != PadBindingExpandStatus::UNCHANGED || strcmp(out, "[mqtt:solar/power;$.value]") != 0) {
        printf("  FAIL [expand no tokens]: got \"%s\" (expanded=%d)\n", out, static_cast<int>(expanded));
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_expand_unknown_binding() {
    printf("--- pad_binding_expand: unknown binding ---\n");
    auto page = make_page({{"power", "[mqtt:solar/power;$.value]"}});

    char out[256];
    auto expanded = pad_binding_expand(&page, "[pad:unknown]", out, sizeof(out));
    // Unknown binding stays as-is
    if (expanded != PadBindingExpandStatus::UNAVAILABLE || strcmp(out, "[pad:unknown]") != 0) {
        printf("  FAIL [expand unknown]: got \"%s\" (expanded=%d)\n", out, static_cast<int>(expanded));
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
    auto expanded = pad_binding_expand(&page, "[pad:power;%.2f]", out, sizeof(out));
    if (expanded != PadBindingExpandStatus::UNAVAILABLE || strcmp(out, "[pad:power;%.2f]") != 0) {
        printf("  FAIL [expand with format]: got \"%s\" (expanded=%d)\n", out, static_cast<int>(expanded));
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_expand_recursive_guard() {
    printf("--- pad_binding_expand: recursive guard ---\n");
    auto page = make_page({
        {"a", "[pad:b]"},
        {"b", "[pad:a]"},
    });

    char out[256];
    auto expanded = pad_binding_expand(&page, "[pad:a]", out, sizeof(out));
    if (expanded != PadBindingExpandStatus::UNAVAILABLE || strcmp(out, "[pad:a]") != 0) {
        printf("  FAIL [expand recursive]: got \"%s\" (expanded=%d)\n", out, static_cast<int>(expanded));
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
    auto expanded = pad_binding_expand(&page,
        "[expr:[pad:solar] - [pad:grid]]",
        out, sizeof(out));
    if (expanded != PadBindingExpandStatus::EXPANDED || strcmp(out, "[expr:[mqtt:solar/power;$.value] - [mqtt:grid/power;$.watts]]") != 0) {
        printf("  FAIL [expand multiple]: got \"%s\"\n", out);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_expand_null_page() {
    printf("--- pad_binding_expand: null page ---\n");
    char out[256];
    auto expanded = pad_binding_expand(nullptr, "[pad:power]", out, sizeof(out));
    if (expanded != PadBindingExpandStatus::UNAVAILABLE || strcmp(out, "[pad:power]") != 0) {
        printf("  FAIL [expand null page]: got \"%s\" (expanded=%d)\n", out, static_cast<int>(expanded));
        g_fail++;
    } else {
        g_pass++;
    }
}

// ============================================================================
// pad_resolve() batch helper
// ============================================================================

static void test_pad_resolve_batch() {
    printf("--- pad_resolve() batch helper ---\n");
    g_mqtt_values["solar/power;$.value"] = "3200";

    PadBinding binds[1] = {};
    strlcpy(binds[0].name,  "power", PAD_BINDING_NAME_MAX_LEN);
    strlcpy(binds[0].value, "[mqtt:solar/power;$.value]", CONFIG_LABEL_MAX_LEN);

    const char* inputs[3] = {
        "[pad:power] W",                      // pad context applied
        "[expr:[pad:power] / 1000;%.1f]",     // expr + format through pad
        "plain text",                         // literal passthrough
    };
    const size_t stride = BINDING_TEMPLATE_MAX_LEN;
    char out[3 * BINDING_TEMPLATE_MAX_LEN];
    g_subscription_checks.clear();
    pad_resolve(inputs, 3, binds, 1, out, stride);

    check_str(out + 0 * stride, "3200 W", "pad_resolve mqtt + suffix");
    check_str(out + 1 * stride, "3.2",    "pad_resolve expr format");
    check_str(out + 2 * stride, "plain text", "pad_resolve literal passthrough");

    if (g_subscription_checks.size() != 3 ||
        g_subscription_checks[0] != inputs[0] ||
        g_subscription_checks[1] != inputs[1] ||
        g_subscription_checks[2] != inputs[2]) {
        printf("  FAIL [pad_resolve subscription checks]: got %zu call(s)\n",
               g_subscription_checks.size());
        g_fail++;
    } else {
        g_pass++;
    }

    // pad_resolve clears the context afterwards, so a later [pad:] token with no
    // context set resolves to the placeholder (mirrors the live per-frame reset).
    char one[BINDING_TEMPLATE_MAX_LEN];
    binding_template_resolve("[pad:power]", one, sizeof(one));
    check_str(one, "---", "pad_resolve clears context after");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== pad binding integration tests ===\n\n");

    // Register schemes (order: inner before outer)
    const BindingSchemeSpec free_form = {1, 3, 2, -1, BINDING_VALIDATION_STANDARD, true, nullptr, nullptr, nullptr};
    const BindingSchemeSpec expression = {1, 2, 1, 1, BINDING_VALIDATION_EXPRESSION, true, nullptr, nullptr, nullptr};
    binding_template_register("mqtt",   mock_mqtt_resolve,   mock_mqtt_collect, free_form);
    binding_template_register("health", mock_health_resolve, mock_health_collect, free_form);
    binding_template_register("expr",   expr_test_resolve,   expr_test_collect, expression);
    binding_template_register("loaded", mock_loaded_resolve, nullptr, free_form);
    pad_binding_init();  // registers "pad" scheme

    // Resolution tests
    test_basic_resolution();
    test_checked_loaded_alias();
    test_format_override();
    test_with_static_text();
    test_inside_expr();
    test_expr_format_with_pad();
    test_unknown_binding_name();
    test_no_page_context();
    test_empty_params();
    test_recursive_guard();
    test_composition_limits();
    test_shared_dependencies_and_work_limit();
    test_set_bindings_api();
    test_multiple_pad_tokens();
    test_health_in_pad_binding();
    test_pad_topic_collection();
    test_pad_resolve_batch();

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

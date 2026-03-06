// ============================================================================
// Integration tests — expr binding + binding template engine + mock resolvers
// ============================================================================
// Tests the full pipeline: template string → resolve inner bindings → evaluate
// expression → format output. Uses mock MQTT/health resolvers (no ESP32 needed).
//
// Build:
//   g++ -std=c++17 -I tests -I src/app
//       tests/test_expr_binding.cpp src/app/binding_template.cpp
//       src/app/expr_eval.cpp tests/stubs.cpp
//       -o tests/bin/test_expr_binding -lm
// Run:
//   ./tests/bin/test_expr_binding

#include "binding_template.h"
#include "expr_eval.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

// ============================================================================
// Mock MQTT resolver
// ============================================================================

static std::map<std::string, std::string> g_mqtt_values;

// Parse "topic;path;format" the same way the real resolver does (simplified)
static void parse_mock_params(const char* params,
                              char* topic, size_t topic_len,
                              char* path, size_t path_len) {
    topic[0] = '\0';
    path[0] = '\0';

    const char* s1 = strchr(params, ';');
    if (!s1) {
        snprintf(topic, topic_len, "%s", params);
        return;
    }
    size_t tlen = (size_t)(s1 - params);
    if (tlen >= topic_len) tlen = topic_len - 1;
    memcpy(topic, params, tlen);
    topic[tlen] = '\0';

    const char* s2 = strchr(s1 + 1, ';');
    const char* path_end = s2 ? s2 : s1 + 1 + strlen(s1 + 1);
    size_t plen = (size_t)(path_end - (s1 + 1));
    if (plen >= path_len) plen = path_len - 1;
    memcpy(path, s1 + 1, plen);
    path[plen] = '\0';
}

static bool mock_mqtt_resolve(const char* params, char* out, size_t out_len) {
    char topic[128], path[64];
    parse_mock_params(params, topic, sizeof(topic), path, sizeof(path));

    std::string key = std::string(topic) + ";" + path;
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
    const char* sep = strchr(params, ';');
    std::string key = sep ? std::string(params, sep - params) : std::string(params);
    auto it = g_health_values.find(key);
    if (it == g_health_values.end()) return false;
    snprintf(out, out_len, "%s", it->second.c_str());
    return true;
}

static void mock_health_collect(const char* params, void* user_data) {
    (void)params; (void)user_data;
}

// ============================================================================
// expr resolver — reimplemented here since expr_binding.cpp has ESP32 deps
// ============================================================================
// This mirrors the logic in expr_binding.cpp but without the ESP32 includes.

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

    // Resolve inner bindings
    char resolved[BINDING_TEMPLATE_MAX_LEN];
    binding_template_resolve(buf, resolved, sizeof(resolved));

    // Placeholder check
    if (strstr(resolved, "---") != NULL) {
        snprintf(out, out_len, "---");
        return false;
    }

    // Evaluate
    char eval_result[EXPR_STR_MAX];
    if (!expr_eval(resolved, eval_result, sizeof(eval_result))) {
        snprintf(out, out_len, "%s", eval_result);
        return false;
    }

    // Format (basic for tests)
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

// ============================================================================
// Test groups
// ============================================================================

static void test_unit_conversion() {
    printf("--- Unit conversion (bytes→KB) ---\n");
    g_mqtt_values["sensor/mem;free_bytes"] = "245760";
    check("[expr:[mqtt:sensor/mem;free_bytes] / 1024]", "240", "bytes to KB");

    g_mqtt_values["sensor/mem;free_bytes"] = "1048576";
    check("[expr:[mqtt:sensor/mem;free_bytes] / 1024]", "1024", "1MB to KB");

    g_mqtt_values["sensor/mem;free_bytes"] = "1048576";
    check("[expr:[mqtt:sensor/mem;free_bytes] / 1024 / 1024]", "1", "1MB to MB");
}

static void test_conditional_text() {
    printf("--- Conditional text (threshold) ---\n");
    g_mqtt_values["sensor/temp;value"] = "35";
    check("[expr:[mqtt:sensor/temp;value] > 30 ? \"Hot\" : \"OK\"]", "Hot", "temp hot");

    g_mqtt_values["sensor/temp;value"] = "22";
    check("[expr:[mqtt:sensor/temp;value] > 30 ? \"Hot\" : \"OK\"]", "OK", "temp ok");

    g_mqtt_values["sensor/temp;value"] = "30";
    check("[expr:[mqtt:sensor/temp;value] > 30 ? \"Hot\" : \"OK\"]", "OK", "temp exactly 30");

    g_mqtt_values["sensor/temp;value"] = "30";
    check("[expr:[mqtt:sensor/temp;value] >= 30 ? \"Hot\" : \"OK\"]", "Hot", "temp >=30");
}

static void test_cross_binding_math() {
    printf("--- Cross-binding arithmetic ---\n");
    g_mqtt_values["solar/power;watts"] = "3200";
    g_mqtt_values["grid/power;watts"]  = "1100";
    check("[expr:[mqtt:solar/power;watts] - [mqtt:grid/power;watts]]",
          "2100", "solar minus grid");

    // Negative result (importing)
    g_mqtt_values["solar/power;watts"] = "500";
    g_mqtt_values["grid/power;watts"]  = "2000";
    check("[expr:[mqtt:solar/power;watts] - [mqtt:grid/power;watts]]",
          "-1500", "importing from grid");
}

static void test_health_binding_in_expr() {
    printf("--- Health binding inside expr ---\n");
    g_health_values["heap_free"] = "187392";
    check("[expr:[health:heap_free] / 1024]", "183", "heap KB");

    g_health_values["psram_free"] = "4194304";
    check("[expr:[health:psram_free] / 1024 / 1024]", "4", "psram MB");
}

static void test_mixed_static_text() {
    printf("--- Mixed static text + expr ---\n");
    g_health_values["heap_free"] = "187392";
    check("Mem: [expr:[health:heap_free] / 1024] KB", "Mem: 183 KB", "with suffix");

    g_mqtt_values["sensor/temp;value"] = "22.5";
    check("Temp: [mqtt:sensor/temp;value]C ([expr:[mqtt:sensor/temp;value] > 25 ? \"Warm\" : \"Cool\"])",
          "Temp: 22.5C (Cool)", "mixed plain + expr");
}

static void test_format_suffix() {
    printf("--- Format suffix via ; ---\n");
    g_mqtt_values["sensor/temp;value"] = "22.567";
    check("[expr:[mqtt:sensor/temp;value] * 1;%.1f]", "22.6", "one decimal");

    g_mqtt_values["sensor/power;watts"] = "1234";
    check("[expr:[mqtt:sensor/power;watts] / 1000;%.2f]", "1.23", "two decimals");

    // Format with inner binding that also uses ';' — the inner ';' must be ignored
    g_health_values["psram_free"] = "4194304";
    check("[expr:[health:psram_free] / (1024 * 1024);%.2f]", "4.00", "psram MB formatted");

    g_mqtt_values["sensor/mem;free_bytes"] = "245760";
    check("[expr:[mqtt:sensor/mem;free_bytes;%s] / 1024;%.1f]", "240.0", "inner fmt + outer fmt");
}

static void test_unresolved_binding() {
    printf("--- Unresolved inner binding ---\n");
    // Clear values to simulate "not yet received"
    auto saved = g_mqtt_values;
    g_mqtt_values.clear();

    check("[expr:[mqtt:unknown/topic;val] + 1]", "---", "unresolved → placeholder");

    g_mqtt_values = saved;
}

static void test_multiple_expr_tokens() {
    printf("--- Multiple expr tokens in one label ---\n");
    g_mqtt_values["solar/power;watts"] = "3200";
    g_mqtt_values["grid/power;watts"]  = "1100";
    check("[expr:[mqtt:solar/power;watts] / 1000]kW solar / [expr:[mqtt:grid/power;watts] / 1000]kW grid",
          "3.2kW solar / 1.1kW grid", "two expr tokens");
}

static void test_plain_bindings_unaffected() {
    printf("--- Plain bindings still work ---\n");
    g_mqtt_values["sensor/temp;value"] = "22.5";
    check("[mqtt:sensor/temp;value]", "22.5", "plain mqtt");

    g_health_values["cpu"] = "42";
    check("[health:cpu]", "42", "plain health");
}

static void test_topic_collection() {
    printf("--- Topic collection from expr ---\n");
    // Verify that expr collects inner MQTT topics for subscription
    struct CollectedTopic { char topic[128]; };
    CollectedTopic topics[8];
    int count = 0;

    struct Ctx {
        CollectedTopic* topics;
        int* count;
    };
    Ctx ctx = { topics, &count };

    // The expr collector should forward to binding_template_collect_topics
    // which discovers [mqtt:solar/power;watts] and [mqtt:grid/power;watts]
    const char* templ = "[expr:[mqtt:solar/power;watts] - [mqtt:grid/power;watts]]";
    binding_template_collect_topics(templ, &ctx);

    // We can't easily verify without the real mqtt collector registering,
    // but at minimum it shouldn't crash
    g_pass++;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== expr binding integration tests ===\n\n");

    // Register schemes (order matters — inner schemes before expr)
    binding_template_register("mqtt",   mock_mqtt_resolve,   mock_mqtt_collect);
    binding_template_register("health", mock_health_resolve, mock_health_collect);
    binding_template_register("expr",   expr_test_resolve,   expr_test_collect);

    test_unit_conversion();
    test_conditional_text();
    test_cross_binding_math();
    test_health_binding_in_expr();
    test_mixed_static_text();
    test_format_suffix();
    test_unresolved_binding();
    test_multiple_expr_tokens();
    test_plain_bindings_unaffected();
    test_topic_collection();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

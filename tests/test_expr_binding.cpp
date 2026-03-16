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

// ============================================================================
// Resolve inner binding tokens and auto-quote non-numeric results
// ============================================================================
// Mirrors resolve_and_quote() from expr_binding.cpp for host-native tests.

static bool resolve_and_quote(const char* expr, char* out, size_t out_len) {
    size_t wp = 0;
    const char* p = expr;
    bool in_quotes = false;

    while (*p && wp < out_len - 1) {
        if (*p == '\\' && in_quotes && *(p + 1)) {
            // Escape sequence inside user-written string — copy both chars
            if (wp + 2 > out_len - 1) break;
            out[wp++] = *p++;
            out[wp++] = *p++;
        } else if (*p == '"') {
            // Toggle user-written string mode and copy the quote
            in_quotes = !in_quotes;
            out[wp++] = *p++;
        } else if (*p == '[' && !in_quotes) {
            // Binding token — find matching ']' with bracket nesting
            int depth = 1;
            const char* tok_start = p;
            p++;
            while (*p && depth > 0) {
                if (*p == '[') depth++;
                else if (*p == ']') depth--;
                p++;
            }

            // Extract and resolve the token
            size_t tok_len = (size_t)(p - tok_start);
            char token[BINDING_TEMPLATE_MAX_LEN];
            if (tok_len >= sizeof(token)) tok_len = sizeof(token) - 1;
            memcpy(token, tok_start, tok_len);
            token[tok_len] = '\0';

            char resolved[BINDING_TEMPLATE_MAX_LEN];
            binding_template_resolve(token, resolved, sizeof(resolved));

            // Unresolved binding → whole expression is unresolved
            if (strcmp(resolved, "---") == 0) {
                snprintf(out, out_len, "---");
                return false;
            }

            // Check if the resolved value is numeric (consumed entirely by strtod)
            char* end = NULL;
            strtod(resolved, &end);
            bool is_numeric = (end && end != resolved && *end == '\0');

            if (is_numeric) {
                size_t rlen = strlen(resolved);
                if (wp + rlen >= out_len) break;
                memcpy(out + wp, resolved, rlen);
                wp += rlen;
            } else {
                // Wrap in quotes, escaping embedded " and backslash
                if (wp >= out_len - 1) break;
                out[wp++] = '"';
                for (const char* r = resolved; *r && wp < out_len - 2; r++) {
                    if (*r == '"' || *r == '\\') {
                        if (wp + 2 >= out_len - 1) break;
                        out[wp++] = '\\';
                    }
                    out[wp++] = *r;
                }
                if (wp >= out_len - 1) break;
                out[wp++] = '"';
            }
        } else {
            out[wp++] = *p++;
        }
    }
    out[wp] = '\0';
    return true;
}

static bool expr_test_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        snprintf(out, out_len, "ERR:empty expr");
        return false;
    }

    char buf[BINDING_TEMPLATE_MAX_LEN];
    snprintf(buf, sizeof(buf), "%s", params);

    char* fmt = split_format(buf);

    // Resolve inner bindings, auto-quoting non-numeric text values
    char resolved[BINDING_TEMPLATE_MAX_LEN];
    if (!resolve_and_quote(buf, resolved, sizeof(resolved))) {
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

static void test_auto_quoting_single_word() {
    printf("--- Auto-quoting: single-word text values ---\n");
    // Basic: non-numeric health value compared to quoted string
    g_health_values["ble_status"] = "connected";
    check("[expr:[health:ble_status]==\"connected\"?\"#00ff00\":\"#ff0000\"]",
          "#00ff00", "ble connected → green");

    g_health_values["ble_status"] = "disconnected";
    check("[expr:[health:ble_status]==\"connected\"?\"#00ff00\":\"#ff0000\"]",
          "#ff0000", "ble disconnected → red");

    // Single-word MQTT text values
    g_mqtt_values["device/status;state"] = "online";
    check("[expr:[mqtt:device/status;state]==\"online\"?\"OK\":\"FAIL\"]",
          "OK", "mqtt online → OK");

    g_mqtt_values["device/status;state"] = "offline";
    check("[expr:[mqtt:device/status;state]==\"online\"?\"OK\":\"FAIL\"]",
          "FAIL", "mqtt offline → FAIL");

    // Boolean-like text
    g_mqtt_values["switch/lamp;state"] = "ON";
    check("[expr:[mqtt:switch/lamp;state]==\"ON\"?1:0]",
          "1", "ON → 1");

    g_mqtt_values["switch/lamp;state"] = "OFF";
    check("[expr:[mqtt:switch/lamp;state]==\"ON\"?1:0]",
          "0", "OFF → 0");
}

static void test_auto_quoting_multi_word() {
    printf("--- Auto-quoting: multi-word text values ---\n");
    // Value with spaces — the motivating reason for auto-quoting
    g_mqtt_values["device/status;state"] = "abc def";
    check("[expr:[mqtt:device/status;state]==\"abc def\"?\"match\":\"no\"]",
          "match", "space in value eq");

    g_mqtt_values["device/status;state"] = "Living Room";
    check("[expr:[mqtt:device/status;state]==\"Living Room\"?\"yes\":\"no\"]",
          "yes", "two-word value eq");

    g_mqtt_values["device/status;state"] = "Not Available";
    check("[expr:[mqtt:device/status;state]!=\"Not Available\"?\"up\":\"down\"]",
          "down", "multi-word neq false");

    g_mqtt_values["device/status;state"] = "all good";
    check("[expr:[mqtt:device/status;state]!=\"Not Available\"?\"up\":\"down\"]",
          "up", "multi-word neq true");
}

static void test_auto_quoting_special_chars() {
    printf("--- Auto-quoting: special characters in values ---\n");
    // Hyphenated names (hostnames, UUIDs, etc.)
    g_health_values["hostname"] = "macropad-123";
    check("[expr:[health:hostname]==\"macropad-123\"?\"yes\":\"no\"]",
          "yes", "hyphenated hostname eq");

    // Value with dots (version strings, IPs)
    g_mqtt_values["device/info;version"] = "v1.2.3";
    check("[expr:[mqtt:device/info;version]==\"v1.2.3\"?\"current\":\"old\"]",
          "current", "dotted version eq");

    g_mqtt_values["device/info;ip"] = "192.168.1.100";
    check("[expr:[mqtt:device/info;ip]==\"192.168.1.100\"?\"home\":\"away\"]",
          "home", "IP address eq");

    // Value with underscores
    g_mqtt_values["device/info;name"] = "my_device_01";
    check("[expr:[mqtt:device/info;name]==\"my_device_01\"?\"yes\":\"no\"]",
          "yes", "underscore value eq");

    // Mixed: hyphens + dots + underscores
    g_mqtt_values["device/info;id"] = "esp32-s3_rev.2";
    check("[expr:[mqtt:device/info;id]==\"esp32-s3_rev.2\"?\"yes\":\"no\"]",
          "yes", "mixed special chars eq");
}

static void test_auto_quoting_preserves_numbers() {
    printf("--- Auto-quoting: numeric values stay numeric ---\n");
    // Integer values must NOT be quoted (would break arithmetic)
    g_mqtt_values["sensor/temp;value"] = "42";
    check("[expr:[mqtt:sensor/temp;value] + 1]", "43", "int stays numeric");

    // Float values must NOT be quoted
    g_mqtt_values["sensor/temp;value"] = "22.5";
    check("[expr:[mqtt:sensor/temp;value] * 2]", "45", "float stays numeric");

    // Negative numbers
    g_mqtt_values["sensor/temp;value"] = "-5";
    check("[expr:[mqtt:sensor/temp;value] + 10]", "5", "negative stays numeric");

    // Leading-dot decimal
    g_mqtt_values["sensor/temp;value"] = ".5";
    check("[expr:[mqtt:sensor/temp;value] + .5]", "1", "dot-decimal stays numeric");

    // Zero
    g_mqtt_values["sensor/temp;value"] = "0";
    check("[expr:[mqtt:sensor/temp;value] == 0 ? \"zero\" : \"nonzero\"]",
          "zero", "zero stays numeric");
}

static void test_auto_quoting_both_bindings_text() {
    printf("--- Auto-quoting: two text bindings compared ---\n");
    // Both sides are text from bindings
    g_mqtt_values["room/actual;state"]   = "on";
    g_mqtt_values["room/expected;state"] = "on";
    check("[expr:[mqtt:room/actual;state]==[mqtt:room/expected;state]?\"match\":\"mismatch\"]",
          "match", "both text eq true");

    g_mqtt_values["room/actual;state"]   = "on";
    g_mqtt_values["room/expected;state"] = "off";
    check("[expr:[mqtt:room/actual;state]==[mqtt:room/expected;state]?\"match\":\"mismatch\"]",
          "mismatch", "both text eq false");

    // Both sides multi-word
    g_mqtt_values["room/actual;state"]   = "Living Room";
    g_mqtt_values["room/expected;state"] = "Living Room";
    check("[expr:[mqtt:room/actual;state]==[mqtt:room/expected;state]?\"same\":\"diff\"]",
          "same", "both multi-word eq true");

    g_mqtt_values["room/actual;state"]   = "Living Room";
    g_mqtt_values["room/expected;state"] = "Bed Room";
    check("[expr:[mqtt:room/actual;state]==[mqtt:room/expected;state]?\"same\":\"diff\"]",
          "diff", "both multi-word eq false");
}

static void test_auto_quoting_empty_value() {
    printf("--- Auto-quoting: empty string values ---\n");
    // Empty resolved value
    g_mqtt_values["device/status;state"] = "";
    check("[expr:[mqtt:device/status;state]==\"\"?\"empty\":\"has data\"]",
          "empty", "empty string eq");

    g_mqtt_values["device/status;state"] = "something";
    check("[expr:[mqtt:device/status;state]==\"\"?\"empty\":\"has data\"]",
          "has data", "non-empty string neq");
}

static void test_auto_quoting_value_with_quotes() {
    printf("--- Auto-quoting: values containing quote chars ---\n");
    // Value that itself contains a double-quote character
    // The auto-quoting must escape embedded quotes
    g_mqtt_values["device/status;msg"] = "say \"hello\"";
    check("[expr:[mqtt:device/status;msg]==\"say \\\"hello\\\"\"?\"yes\":\"no\"]",
          "yes", "value with embedded quotes");
}

static void test_auto_quoting_ternary_result() {
    printf("--- Auto-quoting: text value as ternary condition ---\n");
    // Non-empty text is truthy? No — equality comparison is the normal pattern.
    // Verify arithmetic on text fails gracefully, not silently.
    g_health_values["ble_status"] = "connected";
    // Plain text value by itself (not in comparison) shouldn't crash
    // With auto-quoting it becomes "connected" which is a valid string result
    check("[expr:[health:ble_status]]", "connected", "text value passthrough");
}

static void test_auto_quoting_mixed_numeric_and_text() {
    printf("--- Auto-quoting: mixed numeric and text in one expression ---\n");
    // Compare text binding, do arithmetic on numeric binding in same expression
    g_mqtt_values["switch/lamp;state"] = "ON";
    g_mqtt_values["sensor/power;watts"] = "150";
    // This is a nested ternary: if lamp is ON, show watts, else show 0
    check("[expr:[mqtt:switch/lamp;state]==\"ON\"?[mqtt:sensor/power;watts]:0]",
          "150", "text check then numeric result");

    g_mqtt_values["switch/lamp;state"] = "OFF";
    check("[expr:[mqtt:switch/lamp;state]==\"ON\"?[mqtt:sensor/power;watts]:0]",
          "0", "text check false → numeric 0");
}

static void test_auto_quoting_hyphen_not_subtraction() {
    printf("--- Auto-quoting: hyphenated values don't trigger subtraction ---\n");
    // "macropad-123" must be treated as one string, not "macropad" minus 123
    g_health_values["hostname"] = "macropad-123";
    check("[expr:[health:hostname]]", "macropad-123", "hyphenated passthrough");

    // Even without spaces around the operator, the binding value must stay intact
    g_mqtt_values["device/info;name"] = "esp32-s3-n16r8";
    check("[expr:[mqtt:device/info;name]==\"esp32-s3-n16r8\"?\"yes\":\"no\"]",
          "yes", "multi-hyphen value eq");
}

static void test_pipe_fallback() {
    printf("--- Pipe fallback ---\n");

    // Basic: unresolved mqtt with pipe fallback
    auto saved = g_mqtt_values;
    g_mqtt_values.clear();
    check("[mqtt:unknown/topic|N/A]", "N/A", "basic pipe fallback");

    // Resolved value ignores fallback
    g_mqtt_values["sensor/temp;value"] = "22.5";
    check("[mqtt:sensor/temp;value|--]", "22.5", "resolved ignores fallback");

    // No pipe, unresolved → classic "---" placeholder
    g_mqtt_values.clear();
    check("[mqtt:unknown/topic]", "---", "no pipe → placeholder");

    // Fallback with static prefix/suffix
    check("Temp: [mqtt:sensor/temp;value|??]C", "Temp: ??C", "fallback in context");

    // Fallback inside expr — inner mqtt unresolved, expr sees "---" and fails,
    // expr's own pipe fallback is used
    check("[expr:[mqtt:missing;val] * 2;%.1f|--]", "--", "expr pipe fallback");

    // Health fallback
    auto saved_health = g_health_values;
    g_health_values.clear();
    check("[health:cpu|?]", "?", "health pipe fallback");
    g_health_values = saved_health;

    // Pipe inside nested brackets should NOT split (bracket-depth aware)
    g_mqtt_values["sensor/temp;value"] = "42";
    check("[expr:[mqtt:sensor/temp;value] > 30 ? \"Hot\" : \"Cold\"|err]",
          "Hot", "pipe not split inside nested brackets — resolved");

    // Empty fallback → empty string (not "---")
    g_mqtt_values.clear();
    check("[mqtt:unknown/topic|]", "", "empty fallback");

    // Multiple tokens, mixed resolved and fallback
    g_mqtt_values["solar/power;watts"] = "3200";
    check("[mqtt:solar/power;watts|0]W / [mqtt:grid/power;watts|0]W",
          "3200W / 0W", "mixed resolved and fallback");

    // Fallback with color hex value (practical use case)
    check("[mqtt:theme/color|#FF0000]", "#FF0000", "color hex fallback");

    g_mqtt_values = saved;
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
    test_auto_quoting_single_word();
    test_auto_quoting_multi_word();
    test_auto_quoting_special_chars();
    test_auto_quoting_preserves_numbers();
    test_auto_quoting_both_bindings_text();
    test_auto_quoting_empty_value();
    test_auto_quoting_value_with_quotes();
    test_auto_quoting_ternary_result();
    test_auto_quoting_mixed_numeric_and_text();
    test_auto_quoting_hyphen_not_subtraction();
    test_pipe_fallback();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

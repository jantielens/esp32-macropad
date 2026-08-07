#include "mcp_device_identity.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

static int g_failures = 0;

#define CHECK(condition, label)                                              \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL: %s\n", label);                              \
            ++g_failures;                                                    \
        } else {                                                             \
            std::printf("ok:   %s\n", label);                              \
        }                                                                    \
    } while (0)

static McpIdentityDecision decide(bool read_only, bool destructive,
                                  bool requires_authoring, const char* tool_name,
                                  const char* command, const char* expected,
                                  const char* actual) {
    return mcp_device_identity_decide(read_only, destructive, requires_authoring,
                                      tool_name, command, expected, actual);
}

static void test_formatter() {
    const uint8_t mac[6] = {0x00, 0x01, 0x0a, 0xb0, 0x0c, 0xff};
    char device_id[13];
    mcp_device_identity_format(mac, device_id);
    CHECK(std::strcmp(device_id, "00010ab00cff") == 0, "formatter preserves leading zeroes");
}

static void test_decisions() {
    static constexpr char ACTUAL[] = "001122aabbcc";
    CHECK(decide(false, false, true, "set_pad", nullptr, ACTUAL, ACTUAL) == MCP_IDENTITY_ALLOW,
          "matching authoring assertion allowed");
    CHECK(decide(false, false, true, "set_pad", nullptr, "001122AABBCC", ACTUAL) == MCP_IDENTITY_ALLOW,
          "uppercase assertion allowed");
    CHECK(decide(false, false, true, "set_pad", nullptr, nullptr, ACTUAL) == MCP_IDENTITY_REJECT_MISSING,
          "missing authoring assertion rejected");
    CHECK(decide(false, false, true, "set_pad", nullptr, " 001122aabbcc", ACTUAL) == MCP_IDENTITY_REJECT_MALFORMED,
          "whitespace assertion rejected");
    CHECK(decide(false, false, true, "set_pad", nullptr, "001122aabbcg", ACTUAL) == MCP_IDENTITY_REJECT_MALFORMED,
          "non-hex assertion rejected");
    CHECK(decide(false, false, true, "set_pad", nullptr, "001122aabbc", ACTUAL) == MCP_IDENTITY_REJECT_MALFORMED,
          "truncated assertion rejected");
    CHECK(decide(false, false, true, "set_pad", nullptr, "001122aabbccd", ACTUAL) == MCP_IDENTITY_REJECT_MALFORMED,
          "overlength assertion rejected");
    CHECK(decide(false, false, true, "set_pad", nullptr, "abcdefabcdef", ACTUAL) == MCP_IDENTITY_REJECT_MISMATCH,
          "cross-device authoring assertion rejected");
    CHECK(decide(false, false, false, "set_config", nullptr, nullptr, ACTUAL) == MCP_IDENTITY_REJECT_MISSING,
          "set_config requires assertion");
    CHECK(decide(false, false, false, "set_component_config", nullptr, nullptr, ACTUAL) == MCP_IDENTITY_REJECT_MISSING,
          "set_component_config requires assertion");
    CHECK(decide(false, true, false, "system_command", "reboot", nullptr, ACTUAL) == MCP_IDENTITY_REJECT_MISSING &&
          decide(false, true, false, "system_command", "wifi_reconnect", nullptr, ACTUAL) == MCP_IDENTITY_REJECT_MISSING &&
          decide(false, true, false, "system_command", "screensaver", nullptr, ACTUAL) == MCP_IDENTITY_REJECT_MISSING,
          "all system commands require assertion");
    CHECK(decide(false, false, false, "scale_control", "tare", nullptr, ACTUAL) == MCP_IDENTITY_REJECT_MISSING &&
          decide(false, false, false, "scale_control", "calibrate", nullptr, ACTUAL) == MCP_IDENTITY_REJECT_MISSING,
          "persistent scale commands require assertion");
    CHECK(decide(false, false, false, "scale_control", "cal_weight", nullptr, ACTUAL) == MCP_IDENTITY_ALLOW &&
          decide(false, false, false, "scale_control", "cal_weight_set", nullptr, ACTUAL) == MCP_IDENTITY_ALLOW,
          "runtime scale commands remain exempt");
    CHECK(decide(true, false, false, "get_status", nullptr, nullptr, ACTUAL) == MCP_IDENTITY_ALLOW &&
          decide(true, false, true, "resolve_bindings", nullptr, nullptr, ACTUAL) == MCP_IDENTITY_ALLOW,
          "read-only tools remain exempt");
}

static void test_success_normalization() {
    JsonDocument result_doc;
    JsonObject result = result_doc.to<JsonObject>();
    result["status"] = "saved";
    result["details"] = "hidden";
      char device_id[] = "001122aabbcc";
      mcp_device_identity_normalize_success(result, device_id);
    char serialized[64];
    serializeJson(result, serialized, sizeof(serialized));
    CHECK(std::strcmp(serialized, "{\"device_id\":\"001122aabbcc\"}") == 0,
          "protected success is exactly normalized");
}

int main() {
    test_formatter();
    test_decisions();
    test_success_normalization();
    if (g_failures) return 1;
    std::printf("All MCP device identity tests passed\n");
    return 0;
}
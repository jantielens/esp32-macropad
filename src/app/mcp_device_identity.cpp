#include "mcp_device_identity.h"

#if HAS_MCP

#if defined(ESP_PLATFORM)
#include <esp_err.h>
#include <esp_mac.h>
#endif

#include <string.h>

void mcp_device_identity_format(const uint8_t mac[6], char out[13]) {
    static constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 6; ++i) {
        out[i * 2] = hex[(mac[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[mac[i] & 0x0f];
    }
    out[12] = '\0';
}

bool mcp_device_identity_read(char out[13]) {
    if (!out) return false;
#if defined(ESP_PLATFORM)
    uint8_t mac[6] = {};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return false;
    mcp_device_identity_format(mac, out);
    return true;
#else
    out[0] = '\0';
    return false;
#endif
}

static bool is_hex_ascii(char value) {
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

static bool is_valid_device_id(const char* value) {
    if (!value) return false;
    for (size_t i = 0; i < 12; ++i) {
        if (!is_hex_ascii(value[i])) return false;
    }
    return value[12] == '\0';
}

McpIdentityRequirement mcp_device_identity_requirement(bool read_only,
                                                        bool destructive,
                                                        bool requires_authoring,
                                                        const char* tool_name,
                                                        const char* command) {
    if (read_only) return MCP_IDENTITY_EXEMPT;
    if (destructive || requires_authoring ||
        strcmp(tool_name, "set_config") == 0 ||
        strcmp(tool_name, "set_component_config") == 0) {
        return MCP_IDENTITY_REQUIRED;
    }
    if (strcmp(tool_name, "scale_control") == 0) {
        if (!command) return MCP_IDENTITY_CONDITIONAL;
        return strcmp(command, "tare") == 0 || strcmp(command, "calibrate") == 0
            ? MCP_IDENTITY_REQUIRED
            : MCP_IDENTITY_EXEMPT;
    }
    return MCP_IDENTITY_EXEMPT;
}

McpIdentityDecision mcp_device_identity_decide(bool read_only,
                                               bool destructive,
                                               bool requires_authoring,
                                               const char* tool_name,
                                               const char* command,
                                               const char* expected_device_id,
                                               const char* actual_device_id) {
    if (mcp_device_identity_requirement(read_only, destructive, requires_authoring,
                                        tool_name, command) != MCP_IDENTITY_REQUIRED) {
        return MCP_IDENTITY_ALLOW;
    }
    if (!expected_device_id) return MCP_IDENTITY_REJECT_MISSING;
    if (!is_valid_device_id(expected_device_id)) return MCP_IDENTITY_REJECT_MALFORMED;
    return strcmp(expected_device_id, actual_device_id) == 0 ||
           strcasecmp(expected_device_id, actual_device_id) == 0
        ? MCP_IDENTITY_ALLOW
        : MCP_IDENTITY_REJECT_MISMATCH;
}

void mcp_device_identity_normalize_success(JsonObject result,
                                           char device_id[13]) {
    result.clear();
    result["device_id"] = device_id;
}

#endif  // HAS_MCP
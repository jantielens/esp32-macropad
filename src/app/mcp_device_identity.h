#pragma once

#include "board_config.h"

#if HAS_MCP

#include <ArduinoJson.h>

enum McpIdentityRequirement : uint8_t {
    MCP_IDENTITY_EXEMPT,
    MCP_IDENTITY_CONDITIONAL,
    MCP_IDENTITY_REQUIRED,
};

enum McpIdentityDecision : uint8_t {
    MCP_IDENTITY_ALLOW,
    MCP_IDENTITY_REJECT_MISSING,
    MCP_IDENTITY_REJECT_MALFORMED,
    MCP_IDENTITY_REJECT_MISMATCH,
};

// Read the application SoC's immutable factory MAC and format it canonically.
bool mcp_device_identity_read(char out[13]);
void mcp_device_identity_format(const uint8_t mac[6], char out[13]);

McpIdentityRequirement mcp_device_identity_requirement(bool read_only,
                                                        bool destructive,
                                                        bool requires_authoring,
                                                        const char* tool_name,
                                                        const char* command);
McpIdentityDecision mcp_device_identity_decide(bool read_only,
                                               bool destructive,
                                               bool requires_authoring,
                                               const char* tool_name,
                                               const char* command,
                                               const char* expected_device_id,
                                               const char* actual_device_id);

void mcp_device_identity_normalize_success(JsonObject result,
                                           char device_id[13]);

#endif  // HAS_MCP
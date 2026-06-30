#include "mcp_tool_registry.h"

#include "board_config.h"

#if HAS_MCP

#include <string.h>

// ============================================================================
// MCP tool registry storage.
//
// Populated by REGISTER_MCP_TOOL() static initializers at startup. The registry
// holds pointers to statically-allocated McpTool descriptors; it never copies
// or frees them.
// ============================================================================

static constexpr uint8_t MCP_TOOL_REGISTRY_MAX = 48;
static const McpTool* s_tools[MCP_TOOL_REGISTRY_MAX] = {};
static uint8_t s_tool_count = 0;
static uint16_t s_tools_dropped = 0;

bool mcp_tool_register(const McpTool* tool) {
    if (!tool || !tool->name) return false;
    if (s_tool_count >= MCP_TOOL_REGISTRY_MAX) { ++s_tools_dropped; return false; }
    s_tools[s_tool_count++] = tool;
    return true;
}

uint8_t mcp_tool_count() {
    return s_tool_count;
}

uint16_t mcp_tool_dropped() {
    return s_tools_dropped;
}

const McpTool* mcp_tool_at(uint8_t index) {
    if (index >= s_tool_count) return nullptr;
    return s_tools[index];
}

const McpTool* mcp_tool_find(const char* name) {
    if (!name) return nullptr;
    for (uint8_t i = 0; i < s_tool_count; ++i) {
        if (s_tools[i] && s_tools[i]->name && strcmp(s_tools[i]->name, name) == 0) {
            return s_tools[i];
        }
    }
    return nullptr;
}

#endif // HAS_MCP

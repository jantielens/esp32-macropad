// ============================================================================
// mcp_components.cpp — aggregation translation unit for device-class MCP tools.
//
// arduino-cli only compiles .cpp files in the sketch root, so MCP tool files
// that live under src/app/device_classes/**/ must be #include'd here. Each
// included translation unit registers its tools via REGISTER_MCP_TOOL() (see
// mcp_tool_registry.h); the static initializer runs at startup and the tools
// become visible through the /mcp endpoint.
//
// This file mirrors route_components.cpp. It ships empty of device-class
// includes — the core (board-agnostic) tools live in mcp_tools_core.cpp at the
// sketch root and compile directly. Device-class MCP tools are added by a
// separate PRD under IS_* gates, e.g.:
//
//   #if IS_SHUTTER_TESTER
//   #include "device_classes/shutter_tester/mcp/mcp_tools_shutter.cpp"
//   #endif
// ============================================================================

#include "board_config.h"

#if HAS_MCP

// (No device-class MCP tool includes yet.)

#endif // HAS_MCP

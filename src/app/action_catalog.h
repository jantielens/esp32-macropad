#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include <ArduinoJson.h>

// Single source of action-authoring presentation metadata, shared by the web
// portal (GET /api/info?catalog=1) and the MCP capability manifest
// (get_capabilities). Gating mirrors the #if structure in action_dispatch.cpp,
// so an entry appears here exactly when the running build can execute it.
//
// Each emitted entry carries "type", "group", "label", an optional
// "commands" array of {id, label} for multi-command types, an optional
// "command_field" naming the flat JSON command property for a generic portal
// selector, an optional "command_families" array grouping commands, and — when include_field_docs
// is set — a "fields" array of {name, description} describing the flat JSON
// keys an MCP client would author. The portal projection omits field docs;
// group/label/commands/command_families are all it needs to render pickers,
// and dropping the prose keeps the /api/info payload small.
void action_catalog_emit(JsonArray out, bool include_field_docs);

#endif // HAS_DISPLAY || HAS_BUTTON

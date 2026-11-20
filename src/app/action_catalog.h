#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include <ArduinoJson.h>

// Single source of action-authoring presentation metadata, shared by the web
// portal (GET /api/info?catalog=1) and the MCP capability manifest
// (get_capabilities). Registered types use the same availability predicate as
// authoring validation, so an entry appears exactly when this build supports it.
//
// Entries describe their typed operations and arguments for both consumers.
void action_catalog_emit(JsonArray out);

#endif // HAS_DISPLAY || HAS_BUTTON

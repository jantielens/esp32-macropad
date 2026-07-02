#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include <ArduinoJson.h>

// ============================================================================
// Pad validation — single source of truth for pad JSON validity.
//
// Shared by every write path so a bad pad can never be persisted (both
// pad_config_save_raw and the MCP write tools truncate/store in place):
//   - MCP: validate_pad (dry-run) + every set_button/set_buttons/set_pad write
//   - Web portal: the POST /api/pad save handler
//
// Checks: grid bounds, span overflow, template_pad range, pad-level binding
// name/value limits (incl. the one-level [pad:] rule), per-button colors,
// widget types + widget config field caps, action arrays, and binding tokens
// (unknown scheme / bad health key / list provider / timer id) in any field.
//
// Returns nullptr on success, or a short human-readable error string. The
// returned pointer is either a string literal or a static buffer valid until
// the next call — validation is single-threaded (web task), so this is safe.
// ============================================================================
const char* pad_validate(JsonObjectConst pad);

#endif // HAS_DISPLAY

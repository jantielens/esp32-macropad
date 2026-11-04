#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include <ArduinoJson.h>

// Validates every binding token embedded in a persisted action field.
const char* action_validate_binding_tokens(const char* value);

bool action_json_object_valid(const char* value);

// Validates one persisted action object using its registered action contract.
const char* action_validate_json(JsonObjectConst action, bool require_known_type = true,
								 const char* value_token = nullptr);

#endif // HAS_DISPLAY || HAS_BUTTON
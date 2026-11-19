#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include <ArduinoJson.h>

struct ActionArgumentDef;
struct ActionOperationDef;

// Validates every binding token embedded in a persisted action field.
const char* action_validate_binding_tokens(const char* value);

bool action_json_object_valid(const char* value);

// Validate a typed operation envelope against its registered descriptor.
const char* action_operation_validate_json(JsonObjectConst action, bool validate_bindings = true);

const char* action_operation_validate_descriptor(const ActionOperationDef& operation);
const char* action_argument_normalize_text(const ActionArgumentDef& argument, const char* value,
                                           const char** text, int32_t* integer, float* number = nullptr);

// Validates one persisted action object using its registered action contract.
const char* action_validate_json(JsonObjectConst action);

#endif // HAS_DISPLAY || HAS_BUTTON
#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "pad_config.h"  // ButtonAction, ACTION_TYPE_*, CONFIG_*_MAX_LEN

#include <ArduinoJson.h>

ActionArgumentBlock* action_storage_parse(ActionStorage& storage,
                                         const ActionOperationDef& operation,
                                         JsonObjectConst arguments);

// Parse a typed operation into a ButtonAction using the active storage scope.
void action_parse(const JsonObject& obj, ButtonAction& act);

// Serialize a ButtonAction struct into a JSON object.
// Only writes non-default fields to keep the JSON compact.
void action_to_json(const ButtonAction& act, JsonObject obj);

#endif // HAS_DISPLAY || HAS_BUTTON

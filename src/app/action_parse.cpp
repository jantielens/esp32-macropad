#include "action_parse.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_registry.h"
#include "action_storage.h"
#include "action_validate.h"
#include "log_manager.h"
#include <string.h>

static void copy_action_strings(JsonVariant value) {
    if (value.is<JsonObject>()) {
        for (JsonPair field : value.as<JsonObject>()) copy_action_strings(field.value());
    } else if (value.is<JsonArray>()) {
        for (JsonVariant item : value.as<JsonArray>()) copy_action_strings(item);
    } else if (value.is<const char*>()) {
        const JsonString text = value.as<JsonString>();
        if (text.isLinked()) value.set(JsonString(text.c_str(), text.size(), JsonString::Copied));
    }
}

void action_parse(const JsonObject& a, ButtonAction& act) {
    memset(&act, 0, sizeof(ButtonAction));
    ActionStorage* storage = action_storage_current();
    const char* error = action_operation_validate_json(a, false);
    const ActionOperationDef* operation = action_operation_find(a["op"] | "");
    if (!storage || error || !operation) {
        if (storage) storage->failed = true;
        LOGW("Action", "Cannot parse operation: %s", error ? error : "no argument storage");
        return;
    }
    ActionArgumentBlock* arguments = action_storage_parse(*storage, *operation, a["args"]);
    if (!arguments) {
        LOGE("Action", "Cannot allocate operation arguments");
        return;
    }
    act.type[0] = '@';
    act.payload.operation = {operation, arguments};
}

void action_to_json(const ButtonAction& act, JsonObject obj) {
    if (!act.type[0]) return;  // empty action → empty object
    const ActionOperationDef* operation = action_operation_for_action(act);
    if (operation && act.payload.operation.descriptor == operation && act.payload.operation.arguments) {
        obj["op"] = operation->id;
        JsonObject arguments = obj.createNestedObject("args");
        const ActionArgumentBlock* block = act.payload.operation.arguments;
        const ActionStoredArgument* values = action_storage_arguments(block);
        for (uint8_t index = 0; index < operation->argument_count; ++index) {
            const ActionArgumentDef& argument = operation->arguments[index];
            const ActionStoredArgument& value = values[index];
            if (value.dynamic) {
                arguments.createNestedObject(argument.name)["$template"] = action_storage_text(block, value);
            } else if (action_argument_is_text(argument.type)) {
                arguments[argument.name] = action_storage_text(block, value);
            } else if (argument.type == ACTION_ARGUMENT_BOOLEAN) {
                arguments[argument.name] = value.integer != 0;
            } else if (argument.type == ACTION_ARGUMENT_NUMBER) {
                arguments[argument.name] = value.number;
            } else {
                arguments[argument.name] = value.integer;
            }
        }
        copy_action_strings(obj);
        return;
    }
    LOGE("Action", "Cannot serialize an invalid or legacy action");
}

#endif // HAS_DISPLAY || HAS_BUTTON

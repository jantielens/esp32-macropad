#include "action_catalog.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_continuation.h"
#include "action_registry.h"

// Built-ins and device-class actions are emitted from the live registry, so
// their metadata and availability share one definition.
void action_catalog_emit(JsonArray actions, bool include_field_docs) {
    for (uint8_t i = 0; i < action_type_count(); ++i) {
        const ActionTypeDef* type = action_type_at(i);
        if (!type || !type->type_name || !action_type_is_supported(type->type_name)) continue;
        JsonObject action = actions.createNestedObject();
        action["type"] = type->type_name;
        ButtonAction parsed{};
        auto emit = [](const char* name, char*, size_t capacity, uint8_t flags, void* opaque) {
            if (!(flags & ACTION_FIELD_EVENT)) return true;
            auto* action = static_cast<JsonObject*>(opaque);
            JsonObject field = (*action)["template_fields"][name].to<JsonObject>();
            field["max"] = capacity - 1;
            return true;
        };
        action_type_visit_fields(type, parsed, emit, &action);
        action["alarm_hook_allowed"] = type->execution == ACTION_EXECUTION_SYNC;
        action["pausable"] = type->execution == ACTION_EXECUTION_PAUSABLE;
        if (type->operation_count) {
            JsonArray operations = action.createNestedArray("operations");
            for (uint8_t index = 0; index < type->operation_count; ++index) {
                const ActionOperationDef& operation = type->operations[index];
                JsonObject entry = operations.createNestedObject();
                entry["id"] = operation.id;
                entry["label"] = operation.label;
                entry["execution"] = operation.execution == ACTION_EXECUTION_SYNC ? "sync"
                    : operation.execution == ACTION_EXECUTION_PAUSABLE ? "pausable" : "unknown";
                entry["pausable"] = operation.execution == ACTION_EXECUTION_PAUSABLE;
                entry["alarm_hook_allowed"] = operation.execution == ACTION_EXECUTION_SYNC;
                JsonArray arguments = entry.createNestedArray("arguments");
                for (uint8_t arg_index = 0; arg_index < operation.argument_count; ++arg_index) {
                    const ActionArgumentDef& argument = operation.arguments[arg_index];
                    JsonObject arg = arguments.createNestedObject();
                    arg["name"] = argument.name;
                    arg["label"] = argument.label;
                    const char* type_name = argument.type == ACTION_ARGUMENT_SCREEN ? "screen"
                        : argument.type == ACTION_ARGUMENT_INTEGER ? "integer"
                        : argument.type == ACTION_ARGUMENT_BOOLEAN ? "boolean"
                        : argument.type == ACTION_ARGUMENT_ENUM ? "enum" : "string";
                    arg["type"] = type_name;
                    arg["dynamic"] = argument.dynamic;
                    arg["example"] = argument.example;
                    arg["control"] = argument.control;
                    if (argument.type == ACTION_ARGUMENT_INTEGER) {
                        arg["min"] = argument.minimum;
                        arg["max"] = argument.maximum;
                        if (argument.nonzero_minimum) arg["nonzero_min"] = argument.nonzero_minimum;
                        if (argument.has_default_integer) arg["default"] = argument.default_integer;
                        if (argument.zero_label) arg["zero_label"] = argument.zero_label;
                    } else if (argument.type == ACTION_ARGUMENT_BOOLEAN && argument.has_default_integer) {
                        arg["default"] = argument.default_integer != 0;
                    } else if (action_argument_is_text(argument.type)) {
                        arg["max_length"] = argument.max_length;
                        if (argument.allow_empty) arg["allow_empty"] = true;
                        if (argument.default_text) arg["default"] = argument.default_text;
                    }
                    if (argument.type == ACTION_ARGUMENT_ENUM) {
                        JsonArray choices = arg.createNestedArray("choices");
                        for (uint8_t choice = 0; choice < argument.choice_count; ++choice)
                            choices.add(argument.choices[choice]);
                        if (argument.choice_labels) {
                            JsonArray labels = arg.createNestedArray("choice_labels");
                            for (uint8_t choice = 0; choice < argument.choice_count; ++choice)
                                labels.add(argument.choice_labels[choice]);
                        }
                    }
                }
            }
        }
        if (type->describe) {
            type->describe(action);
            if (!include_field_docs) action.remove("fields");
        } else {
            action["group"] = "Device";
            action["label"] = type->type_name;
        }
    }
}

#endif // HAS_DISPLAY || HAS_BUTTON

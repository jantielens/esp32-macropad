#include "action_registry.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "../runtime_variables.h"
#if HAS_DISPLAY
#include "../action_dispatch.h"
#include "../display_manager_lock.h"
#endif
#include "log_manager.h"

namespace {
const char* validate_variable_name(const char* name) {
    return runtime_variable_name_is_valid(name) ? nullptr : "invalid variable name";
}

ActionResult store_variable(const char* name, const char* value, const char* label) {
    const RuntimeVariableSetResult result = runtime_variable_set(name, value);
#if HAS_DISPLAY
    if (result == RUNTIME_VARIABLE_STORED && action_dispatch_context().interactive)
        display_manager_request_binding_refresh();
#endif
    if (result == RUNTIME_VARIABLE_STORED || result == RUNTIME_VARIABLE_UNCHANGED)
        return ACTION_COMPLETE;
    LOGW("Action", "%s Set variable failed (%d)", label, static_cast<int>(result));
    return ACTION_FAILED;
}

ActionResult dispatch_variable_set(const ActionResolvedArgument* arguments, const char* label, uint32_t) {
    return store_variable(arguments[0].text, arguments[1].text, label);
}

const ActionArgumentDef kVariableArguments[] = {
    {"name", "Name", ACTION_ARGUMENT_STRING, 0, 0, RUNTIME_VARIABLE_NAME_CAPACITY - 1,
     true, "[item:name]", "variable", nullptr, 0, false, validate_variable_name},
    {"value", "Value", ACTION_ARGUMENT_STRING, 0, 0, RUNTIME_VARIABLE_VALUE_CAPACITY - 1,
     true, "[expr:[var:counter|0]+1]", "text", nullptr, 0, true},
};
const ActionOperationDef kVariableOperations[] = {
    {"variable.set", "Set variable", kVariableArguments, 2, dispatch_variable_set,
     ACTION_EXECUTION_SYNC, true},
};

void describe_set_var(JsonObject& action) {
    action["group"] = "Variables";
    action["label"] = "Set variable";
    action["note"] = "Stores a volatile device-global value. The Value is resolved when the action runs.";

}

DEFINE_AND_REGISTER_ACTION_TYPE(kSetVarActionType, ACTION_TYPE_SET_VAR, describe_set_var, nullptr,
    ACTION_EXECUTION_SYNC, true, true,
    kVariableOperations, sizeof(kVariableOperations) / sizeof(kVariableOperations[0]));
}

#endif
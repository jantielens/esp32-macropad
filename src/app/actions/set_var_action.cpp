#include "action_registry.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "../binding_template.h"
#include "../runtime_variables.h"
#include "log_manager.h"

#include <string.h>

namespace {
static constexpr size_t kSetVarValueCapacity = CONFIG_LABEL_MAX_LEN;

const char* validate_set_var(JsonObjectConst action) {
    if (!action["var_name"].is<const char*>()) return "var_name must be a string";
    const char* name = action["var_name"].as<const char*>();
    const JsonString name_json = action["var_name"].as<JsonString>();
    if (name_json.size() != strlen(name)) return "var_name must not contain NUL";
    if (!runtime_variable_name_is_valid(name)) return "var_name is invalid";
    if (!action["var_value"].is<const char*>()) return "var_value must be a string";
    const char* value = action["var_value"].as<const char*>();
    const JsonString value_json = action["var_value"].as<JsonString>();
    if (value_json.size() != strlen(value)) return "var_value must not contain NUL";
    return value_json.size() < kSetVarValueCapacity
        ? nullptr : "var_value exceeds 191 UTF-8 bytes";
}

void parse_set_var(const JsonObject& action, ButtonAction& act) {
    if (validate_set_var(action)) return;
    strlcpy(act.payload.set_var.var_name, action["var_name"].as<const char*>(),
            sizeof(act.payload.set_var.var_name));
    strlcpy(act.payload.set_var.var_value, action["var_value"].as<const char*>(),
            sizeof(act.payload.set_var.var_value));
}

void serialize_set_var(const ButtonAction& act, JsonObject action) {
    action["var_name"] = act.payload.set_var.var_name;
    action["var_value"] = act.payload.set_var.var_value;
}

class SetVariableActionLock {
public:
    SetVariableActionLock() { runtime_variable_transaction_lock(); }

    ~SetVariableActionLock() { runtime_variable_transaction_unlock(); }
};

ActionResult dispatch_set_var(const ButtonAction& act, const char* label, uint32_t) {
    if (!runtime_variable_name_is_valid(act.payload.set_var.var_name)) return ACTION_FAILED;
    const size_t input_length = strnlen(act.payload.set_var.var_value,
                                        sizeof(act.payload.set_var.var_value));
    if (input_length == sizeof(act.payload.set_var.var_value)) return ACTION_FAILED;

    char resolved[RUNTIME_VARIABLE_VALUE_CAPACITY] = {};
    const char* value = act.payload.set_var.var_value;
    RuntimeVariableSetResult result = RUNTIME_VARIABLE_SET_VALUE_TOO_LONG;
    {
        SetVariableActionLock lock;
        const bool has_bindings = binding_template_has_bindings(value);
        const bool valid = !has_bindings ||
            binding_template_resolve_checked(value, resolved, sizeof(resolved));
        if (valid) result = runtime_variable_set(act.payload.set_var.var_name,
                                                  has_bindings ? resolved : value);
    }

    if (result == RUNTIME_VARIABLE_STORED || result == RUNTIME_VARIABLE_UNCHANGED)
        return ACTION_COMPLETE;
    LOGW("Action", "%s Set variable failed (%d)", label, static_cast<int>(result));
    return ACTION_FAILED;
}

bool visit_set_var_fields(ButtonAction& act, ActionBindableFieldVisitor visitor, void* context) {
    return visitor(act.payload.set_var.var_value, sizeof(act.payload.set_var.var_value), true, context);
}

void describe_set_var(JsonObject& action) {
    action["group"] = "Variables";
    action["label"] = "Set variable";
    action["note"] = "Stores a volatile device-global value. The Value is resolved when the action runs.";
    JsonArray fields = action["editor_fields"].to<JsonArray>();
    JsonObject name = fields.add<JsonObject>();
    name["name"] = "var_name";
    name["label"] = "Name";
    name["type"] = "text";
    JsonObject value = fields.add<JsonObject>();
    value["name"] = "var_value";
    value["label"] = "Value";
    value["type"] = "text";
    value["bindable"] = true;
}

bool set_var_available() { return true; }

DEFINE_AND_REGISTER_ACTION_TYPE(kSetVarActionType, ACTION_TYPE_SET_VAR, parse_set_var,
    serialize_set_var, dispatch_set_var, nullptr, describe_set_var, set_var_available,
    validate_set_var, visit_set_var_fields, ACTION_EXECUTION_SYNC, true, false, true);
}

#endif
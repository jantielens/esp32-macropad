#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_result.h"
#include "pad_config.h"  // ButtonAction
#include <ArduinoJson.h>

// ============================================================================
// Action Type Registry
// ============================================================================
// Vtable-based registry for built-in and device-class action types. The
// registry is the production source of truth for parse/serialize, dispatch,
// availability, validation, catalog metadata, and bindable fields.
//
// All function pointers may be nullptr. parse/serialize are mandatory for
// types with payload data; dispatch is mandatory for types that produce side
// effects.
//
typedef bool (*ActionBindableFieldVisitor)(char* field, size_t field_size,
                                           bool reject_overflow, void* context);
enum ActionFieldFlags : uint8_t {
    ACTION_FIELD_BINDING = 1,
    ACTION_FIELD_EVENT = 2,
    ACTION_FIELD_REJECT_OVERFLOW = 4
};
typedef bool (*ActionFieldVisitor)(const char* name, char* field, size_t capacity,
                                   uint8_t flags, void* context);
typedef bool (*ActionFields)(ButtonAction&, ActionFieldVisitor, void*);

template<size_t capacity>
bool action_visit_field(ActionFieldVisitor visitor, void* context, const char* name,
                        char (&field)[capacity], uint8_t flags = ACTION_FIELD_BINDING |
                            ACTION_FIELD_EVENT | ACTION_FIELD_REJECT_OVERFLOW) {
    return visitor(name, field, capacity, flags, context);
}

// describe() may expose only the existing generic editor field types (text,
// number, select, toggle). Actions with conditional or specialized controls
// keep their custom portal editor while still using this registry contract.
enum ActionExecutionClass { ACTION_EXECUTION_UNKNOWN, ACTION_EXECUTION_SYNC,
                            ACTION_EXECUTION_PAUSABLE };

struct ActionTypeDef {
    constexpr ActionTypeDef(
        const char* type_name,
        void (*parse)(const JsonObject& a, ButtonAction& act),
        void (*serialize)(const ButtonAction& act, JsonObject obj),
        ActionResult (*dispatch)(const ButtonAction& act, const char* label,
                                 uint32_t continuation_token),
        ActionFields fields,
        void (*describe)(JsonObject& out),
        bool (*available)() = nullptr,
                const char* (*validate)(JsonObjectConst action) = nullptr,
        ActionExecutionClass execution = ACTION_EXECUTION_UNKNOWN,
            bool display_lock_required = true,
            bool resolve_bindings_before_dispatch = true,
            bool dispatch_lock_always = false)
        : type_name(type_name), parse(parse), serialize(serialize), dispatch(dispatch),
          fields(fields), describe(describe), available(available),
                    validate(validate), execution(execution),
                    display_lock_required(display_lock_required),
                    resolve_bindings_before_dispatch(resolve_bindings_before_dispatch),
                    dispatch_lock_always(dispatch_lock_always) {}

    const char* type_name;                                                  // matches ButtonAction::type
    void (*parse)(const JsonObject& a, ButtonAction& act);                  // flat JSON -> payload arm
    void (*serialize)(const ButtonAction& act, JsonObject obj);             // payload arm -> flat JSON
    // A nonzero continuation_token reserves the action suffix. Return
    // ACTION_PENDING only after accepting that token; token 0 means another
    // continuation is active and must be rejected with ACTION_FAILED.
    ActionResult (*dispatch)(const ButtonAction& act, const char* label,
                             uint32_t continuation_token);                  // execute side effects
    ActionFields fields;
    void (*describe)(JsonObject& out);                                       // optional: list flat JSON fields for the MCP manifest (nullptr = none)
    bool (*available)();                                                      // optional: false hides and rejects the type on this build
    const char* (*validate)(JsonObjectConst action);                         // optional: authoring validation; nullptr = valid
    ActionExecutionClass execution;
    bool display_lock_required;
    bool resolve_bindings_before_dispatch;
    bool dispatch_lock_always;
};

void action_type_register(const ActionTypeDef* type);
const ActionTypeDef* action_type_find(const char* type_name);

// Registry enumeration (for the MCP capability manifest): device-class action
// types self-register, so the manifest can list them generated, not hand-coded.
uint8_t action_type_count();
const ActionTypeDef* action_type_at(uint8_t index);

// Returns true only when a registered type is available in this build. Use
// this shared decision for authoring validation and catalog discovery.
bool action_type_is_supported(const char* type_name);

// Invoke an action type's optional authoring validator. nullptr means valid.
const char* action_type_validate(const ActionTypeDef* type, JsonObjectConst action);

// Replace every literal token with a value without modifying the field on failure.
bool action_substitute_token_field(char* field, size_t field_size,
                                   const char* token, const char* replacement);
bool action_type_substitute_token(const ActionTypeDef* def, ButtonAction& act,
                                  const char* token, const char* value);
bool action_type_visit_fields(const ActionTypeDef* def, ButtonAction& act,
                              ActionFieldVisitor visitor, void* context);

bool action_type_substitute_step(const ActionTypeDef* def, ButtonAction& act, float step);
bool action_type_visit_bindable_fields(const ActionTypeDef* def, ButtonAction& act,
                                       ActionBindableFieldVisitor visitor, void* context);
bool action_type_has_binding(const ActionTypeDef* def, const ButtonAction& act);
bool action_type_resolve_bindings(const ActionTypeDef* def, ButtonAction& act);
#if HAS_MQTT || HAS_HA_LIVE
// Collect MQTT topics from declared bindable fields into the collector context.
void action_type_collect_topics(const ActionTypeDef* def, const ButtonAction& act, void* user_data);
#endif

// Auto-register an ActionTypeDef instance via a static constructor.
#define REGISTER_ACTION_TYPE(var)                                              \
    static struct var##AutoReg {                                               \
        var##AutoReg() { action_type_register(&var); }                         \
    } _##var##_auto_reg

// Define an immutable action type and register it as one declaration. Use
// this for production action types so the definition cannot be omitted from
// the registry by a separate registration step.
template<size_t capacity>
constexpr bool action_type_name_fits(const char (&)[capacity]) {
    return capacity <= CONFIG_ACTION_TYPE_MAX_LEN;
}

#define DEFINE_AND_REGISTER_ACTION_TYPE(var, type_name, ...)                   \
    static_assert(action_type_name_fits(type_name),                            \
                  "Action identifier exceeds CONFIG_ACTION_TYPE_MAX_LEN; shorten the persisted type name"); \
    const ActionTypeDef var = { type_name, __VA_ARGS__ };                       \
    REGISTER_ACTION_TYPE(var)

#endif // HAS_DISPLAY || HAS_BUTTON

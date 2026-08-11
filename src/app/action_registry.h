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
// value_field supports the common single bindable/numeric value used by
// device-class actions. binding_fields extends that model for built-ins with
// multiple or conditional bindable fields without adding a UI schema.
typedef bool (*ActionBindableFieldVisitor)(char* field, size_t field_size,
                                           bool reject_overflow, void* context);

// describe() may expose only the existing generic editor field types (text,
// number, select, toggle). Actions with conditional or specialized controls
// keep their custom portal editor while still using this registry contract.
struct ActionTypeDef {
    ActionTypeDef(
        const char* type_name,
        void (*parse)(const JsonObject& a, ButtonAction& act),
        void (*serialize)(const ButtonAction& act, JsonObject obj),
        ActionResult (*dispatch)(const ButtonAction& act, const char* label,
                                 uint32_t continuation_token),
        char* (*value_field)(ButtonAction& act, size_t* out_size),
        void (*describe)(JsonObject& out),
        bool (*available)() = nullptr,
                const char* (*validate)(JsonObjectConst action) = nullptr,
                bool (*binding_fields)(ButtonAction& act, ActionBindableFieldVisitor visitor,
                                                             void* context) = nullptr)
        : type_name(type_name), parse(parse), serialize(serialize), dispatch(dispatch),
          value_field(value_field), describe(describe), available(available),
                    validate(validate), binding_fields(binding_fields) {}

    const char* type_name;                                                  // matches ButtonAction::type
    void (*parse)(const JsonObject& a, ButtonAction& act);                  // flat JSON -> payload arm
    void (*serialize)(const ButtonAction& act, JsonObject obj);             // payload arm -> flat JSON
    // A nonzero continuation_token reserves the action suffix. Return
    // ACTION_PENDING only after accepting that token; token 0 means another
    // continuation is active and must be rejected with ACTION_FAILED.
    ActionResult (*dispatch)(const ButtonAction& act, const char* label,
                             uint32_t continuation_token);                  // execute side effects
    char* (*value_field)(ButtonAction& act, size_t* out_size);              // &payload.value (+ buffer size), or nullptr
    void (*describe)(JsonObject& out);                                       // optional: list flat JSON fields for the MCP manifest (nullptr = none)
    bool (*available)();                                                      // optional: false hides and rejects the type on this build
    const char* (*validate)(JsonObjectConst action);                         // optional: authoring validation; nullptr = valid
    bool (*binding_fields)(ButtonAction& act, ActionBindableFieldVisitor visitor,
                           void* context);                                   // optional: visit bindable payload fields
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

// Replace every "{step}" token in a char buffer with the signed step value.
// Canonical helper shared by the numeric rocker and device-class action types
// so {step} substitution behaves identically everywhere.
void action_substitute_step_field(char* field, size_t field_size, float step);

// Generic value-field behaviors driven by ActionTypeDef::value_field. These
// replace the former per-type resolve_bindings / has_binding / substitute_step
// hooks: implement value_field once and the type gets all three. All are
// no-ops when def or def->value_field is nullptr.
void action_type_substitute_step(const ActionTypeDef* def, ButtonAction& act, float step);
bool action_type_visit_bindable_fields(const ActionTypeDef* def, ButtonAction& act,
                                       ActionBindableFieldVisitor visitor, void* context);
#if HAS_MQTT
bool action_type_has_binding(const ActionTypeDef* def, const ButtonAction& act);
bool action_type_resolve_bindings(const ActionTypeDef* def, ButtonAction& act);
// Collect MQTT topics referenced by the type's bindable value field (if any)
// into a binding_template_collect_topics() context. No-op without value_field.
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
#define DEFINE_AND_REGISTER_ACTION_TYPE(var, ...)                              \
    const ActionTypeDef var = { __VA_ARGS__ };                                  \
    REGISTER_ACTION_TYPE(var)

#endif // HAS_DISPLAY || HAS_BUTTON

#pragma once

#include "board_config.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_result.h"
#include "pad_config.h"  // ButtonAction
#include <ArduinoJson.h>

// ============================================================================
// Action Type Registry
// ============================================================================
// Vtable-based registry for action types added by device classes (e.g. shutter
// tester). Built-in action types (screen, mqtt, key, beep, volume,
// brightness, timer, sound, notify, system, back, ble_pair) remain handled
// directly by the strcmp ladders in action_parse.cpp / action_dispatch.cpp;
// the registry is the fallthrough path for any type the ladder does not
// recognize.
//
// All function pointers may be nullptr. parse/serialize are mandatory for
// types with payload data; dispatch is mandatory for types that produce side
// effects.
//
// value_field is the single seam for everything shared code does to a
// device-class payload's bindable/numeric value: binding resolution, the '['
// binding scan, and numeric-rocker {step} substitution. By the universal
// device-class convention every payload is { command, value } with `value`
// as the one bindable field, so exposing value_field gives a type all three
// behaviors at once and removes the per-type "forgot a hook" failure mode.
// Types with no single value field (e.g. shelly) leave it nullptr. An
// implementation writes the value buffer's size to *out_size (callers always
// pass a valid pointer) and returns the field.

struct ActionTypeDef {
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
};

void action_type_register(const ActionTypeDef* type);
const ActionTypeDef* action_type_find(const char* type_name);

// Registry enumeration (for the MCP capability manifest): device-class action
// types self-register, so the manifest can list them generated, not hand-coded.
uint8_t action_type_count();
const ActionTypeDef* action_type_at(uint8_t index);

// Replace every "{step}" token in a char buffer with the signed step value.
// Canonical helper shared by the numeric rocker and device-class action types
// so {step} substitution behaves identically everywhere.
void action_substitute_step_field(char* field, size_t field_size, float step);

// Generic value-field behaviors driven by ActionTypeDef::value_field. These
// replace the former per-type resolve_bindings / has_binding / substitute_step
// hooks: implement value_field once and the type gets all three. All are
// no-ops when def or def->value_field is nullptr.
void action_type_substitute_step(const ActionTypeDef* def, ButtonAction& act, float step);
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

#endif // HAS_DISPLAY || HAS_BUTTON

#pragma once

#include "board_config.h"

#if HAS_DISPLAY

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
// types with payload data; resolve_bindings/has_binding are only needed when
// the type has bindable string fields; dispatch is mandatory for types that
// produce side effects.

struct ActionTypeDef {
    const char* type_name;                                                  // matches ButtonAction::type
    void (*parse)(const JsonObject& a, ButtonAction& act);                  // flat JSON -> payload arm
    void (*serialize)(const ButtonAction& act, JsonObject obj);             // payload arm -> flat JSON
#if HAS_MQTT
    void (*resolve_bindings)(ButtonAction& act);                            // walk bindable fields in-place
    bool (*has_binding)(const ButtonAction& act);                           // quick '[' scan
#endif
    void (*dispatch)(const ButtonAction& act, const char* label);           // execute side effects
};

void action_type_register(const ActionTypeDef* type);
const ActionTypeDef* action_type_find(const char* type_name);

// Auto-register an ActionTypeDef instance via a static constructor.
#define REGISTER_ACTION_TYPE(var)                                              \
    static struct var##AutoReg {                                               \
        var##AutoReg() { action_type_register(&var); }                         \
    } _##var##_auto_reg

#endif // HAS_DISPLAY

// ============================================================================
// Coffee-scale ButtonAction payload arms (scale + brew)
// ============================================================================
// Defines ScalePayload and BrewPayload — both stored in the generic
// ActionPayload::device_class opaque slot (see pad_config.h) when
// ButtonAction::type == ACTION_TYPE_SCALE or ACTION_TYPE_BREW. Lives in the
// device-class folder so the action types' storage cost, wire-strings, and
// accessor patterns all stay inside the device class.
//
// Mirrors the canonical shutter_payload.h pattern.
// ============================================================================

#pragma once

#if IS_COFFEE_SCALE

#include "../../pad_config.h"  // ActionPayload, ButtonAction, ACTION_PAYLOAD_DEVICE_CLASS_BYTES, CONFIG_*

// Wire-format action type discriminators. Live here (not in pad_config.h) so
// shared code stays free of device-class names. Consumers: scale_actions.cpp,
// brew_actions.cpp.
#define ACTION_TYPE_SCALE "scale"
#define ACTION_TYPE_BREW  "brew"

// Per-type command-field widths. CONFIG_TIMER_CMD_MAX_LEN (12) is too small for
// "set_template" (13 incl. NUL) and "cal_weight_set" (15 incl. NUL), causing
// silent strlcpy truncation and "unknown cmd" dispatch failures. Scale and brew
// are independent action types, so each owns its own sizing constant.
#define SCALE_CMD_MAX_LEN 16   // longest: "cal_weight_set" (14 + null). Update if new command strings exceed this length.
#define BREW_CMD_MAX_LEN  13   // longest: "set_template" (12 + null). Update if new command strings exceed this length.

struct ScalePayload {
    // "tare", "calibrate", "cal_weight", "cal_weight_set"
    char command[SCALE_CMD_MAX_LEN];
    // Numeric string (delta or absolute grams), or empty for tare/calibrate
    char value[CONFIG_BINDABLE_SHORT_LEN];
};

struct BrewPayload {
    // "set_template", "advance", "start", "next", "stop", "reset", "tare"
    char command[BREW_CMD_MAX_LEN];
    // Template name (bindable) for set_template; empty for other commands
    char value[CONFIG_BINDABLE_SHORT_LEN];
};

static_assert(sizeof(ScalePayload) <= ACTION_PAYLOAD_DEVICE_CLASS_BYTES,
              "ScalePayload exceeds ACTION_PAYLOAD_DEVICE_CLASS_BYTES — raise it via board_overrides.h");
static_assert(sizeof(BrewPayload) <= ACTION_PAYLOAD_DEVICE_CLASS_BYTES,
              "BrewPayload exceeds ACTION_PAYLOAD_DEVICE_CLASS_BYTES — raise it via board_overrides.h");

// Typed views into ButtonAction::payload::device_class. Only valid when
// act.type matches the corresponding ACTION_TYPE_* discriminator
// (caller-enforced).
inline ScalePayload& scale_payload(ButtonAction& act) {
    return *reinterpret_cast<ScalePayload*>(act.payload.device_class);
}
inline const ScalePayload& scale_payload(const ButtonAction& act) {
    return *reinterpret_cast<const ScalePayload*>(act.payload.device_class);
}
inline BrewPayload& brew_payload(ButtonAction& act) {
    return *reinterpret_cast<BrewPayload*>(act.payload.device_class);
}
inline const BrewPayload& brew_payload(const ButtonAction& act) {
    return *reinterpret_cast<const BrewPayload*>(act.payload.device_class);
}

#endif // IS_COFFEE_SCALE

// ============================================================================
// Shutter-tester ButtonAction payload arm
// ============================================================================
// Defines ShutterPayload — stored in the generic ActionPayload::device_class
// opaque slot (see pad_config.h) when ButtonAction::type == ACTION_TYPE_SHUTTER.
// Lives in the device-class folder so the shutter action type's storage cost,
// wire-string, and accessor pattern all stay inside the device class.
//
// This is the canonical pattern for any device-class action payload. The
// coffee-scale and darkroom-timer feature branches should mirror this header
// (own struct + ACTION_TYPE_* constant + static_assert + shutter_payload-style
// accessors) when they migrate their action types into the opaque slot.
// ============================================================================

#pragma once

#if IS_SHUTTER_TESTER

#include "../../pad_config.h"  // ActionPayload, ButtonAction, ACTION_PAYLOAD_DEVICE_CLASS_BYTES, CONFIG_*

// Wire-format action type discriminator. Lives here (not in pad_config.h) so
// shared code stays free of device-class names. Consumers: shutter_actions.cpp,
// shutter_session_actions.{h,cpp}, tests/test_shutter_session_actions.cpp.
#define ACTION_TYPE_SHUTTER "shutter"

struct ShutterPayload {
    // "set", "adjust", "toggle_lock", "sess_*", "guide_*", "align_*", "recalibrate"
    char command[CONFIG_TIMER_CMD_MAX_LEN];
    // Speed label, "faster"/"slower", camera name, test id, or binding token
    char value[CONFIG_BINDABLE_SHORT_LEN];
};

static_assert(sizeof(ShutterPayload) <= ACTION_PAYLOAD_DEVICE_CLASS_BYTES,
              "ShutterPayload exceeds ACTION_PAYLOAD_DEVICE_CLASS_BYTES — raise it via board_overrides.h");

// Typed view into ButtonAction::payload::device_class. Centralizes the
// reinterpret_cast so call sites read like ordinary named-arm access:
//     shutter_payload(act).command   // read
//     shutter_payload(act).value     // write
// Only valid when act.type == ACTION_TYPE_SHUTTER (caller-enforced).
inline ShutterPayload& shutter_payload(ButtonAction& act) {
    return *reinterpret_cast<ShutterPayload*>(act.payload.device_class);
}
inline const ShutterPayload& shutter_payload(const ButtonAction& act) {
    return *reinterpret_cast<const ShutterPayload*>(act.payload.device_class);
}

#endif // IS_SHUTTER_TESTER

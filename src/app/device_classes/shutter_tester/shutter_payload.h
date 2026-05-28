// ============================================================================
// Shutter-tester ButtonAction payload arm
// ============================================================================
// Defines ShutterPayload — the arm of ActionPayload added to ButtonAction
// when IS_SHUTTER_TESTER is set. Lives in the device-class folder so the
// shutter type's storage cost stays inside the device class.
//
// Header is included by pad_config.h after the CONFIG_* size macros have
// been defined. It is not safe to include standalone.
// ============================================================================

#pragma once

#if IS_SHUTTER_TESTER

#ifndef CONFIG_TIMER_CMD_MAX_LEN
#error "shutter_payload.h must be included via pad_config.h"
#endif

struct ShutterPayload {
    // "set", "adjust", "toggle_lock", "sess_*", "guide_*", "align_*", "recalibrate"
    char command[CONFIG_TIMER_CMD_MAX_LEN];
    // Speed label, "faster"/"slower", camera name, test id, or binding token
    char value[CONFIG_BINDABLE_SHORT_LEN];
};

#endif // IS_SHUTTER_TESTER

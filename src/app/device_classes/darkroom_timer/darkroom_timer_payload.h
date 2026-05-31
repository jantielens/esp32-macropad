// ============================================================================
// Darkroom-timer ButtonAction payload arm (shelly)
// ============================================================================
// Defines ShellyPayload — stored in the generic ActionPayload::device_class
// opaque slot (see pad_config.h) when ButtonAction::type == ACTION_TYPE_SHELLY.
// Lives in the device-class folder so the action type's storage cost,
// wire-strings, and accessor pattern all stay inside the device class.
//
// Mirrors the canonical coffee_scale_payload.h / shutter_payload.h pattern.
// ============================================================================

#pragma once

#if IS_DARKROOM_TIMER

#include "../../pad_config.h"  // ActionPayload, ButtonAction, ACTION_PAYLOAD_DEVICE_CLASS_BYTES

// Wire-format action type discriminator. Lives here (not in pad_config.h) so
// shared code stays free of device-class names. Consumers: shelly_actions.cpp,
// relay_controller.cpp.
#define ACTION_TYPE_SHELLY "shelly"

// Host/hostname field width. Owned by this device class so shared code carries
// no Shelly sizing constant. Do not reuse other action constants.
#define SHELLY_HOST_MAX_LEN 64

struct ShellyPayload {
    // Shelly device IP or hostname (no scheme, no path)
    char    host[SHELLY_HOST_MAX_LEN];
    // Relay index on the Shelly device
    uint8_t relay;
    // true = turn relay on, false = turn relay off
    bool    on;
};

static_assert(sizeof(ShellyPayload) <= ACTION_PAYLOAD_DEVICE_CLASS_BYTES,
              "ShellyPayload exceeds ACTION_PAYLOAD_DEVICE_CLASS_BYTES — raise it via board_overrides.h");

// Typed views into ButtonAction::payload::device_class. Only valid when
// act.type == ACTION_TYPE_SHELLY (caller-enforced).
inline ShellyPayload& shelly_payload(ButtonAction& act) {
    return *reinterpret_cast<ShellyPayload*>(act.payload.device_class);
}
inline const ShellyPayload& shelly_payload(const ButtonAction& act) {
    return *reinterpret_cast<const ShellyPayload*>(act.payload.device_class);
}

#endif // IS_DARKROOM_TIMER

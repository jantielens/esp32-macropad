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

// ============================================================================
// Command/value action payloads (expose, strip, meter, print)
// ============================================================================
// Each darkroom engine action carries a short command verb plus an optional
// value argument. Command buffers are sized to 20 bytes to fit the longest
// verb "adjust_countdown" (16 chars + nul) — matching the field-proven
// CONFIG_TIMER_CMD_MAX_LEN=20 from the legacy branch. Value buffers are 16
// bytes (matching CONFIG_VALUE_MAX_LEN, which supports "{step}" templates).
//
// All four payloads share the same layout but keep distinct type names and
// accessors so the storage cost and wire-strings stay inside this device
// class. Each is well under ACTION_PAYLOAD_DEVICE_CLASS_BYTES (96).

// ── expose: single-exposure countdown timer ─────────────────────────
#define ACTION_TYPE_EXPOSE     "expose"
#define EXPOSE_CMD_MAX_LEN     20
#define EXPOSE_VALUE_MAX_LEN   16

struct ExposePayload {
    char command[EXPOSE_CMD_MAX_LEN];
    char value[EXPOSE_VALUE_MAX_LEN];
};
static_assert(sizeof(ExposePayload) <= ACTION_PAYLOAD_DEVICE_CLASS_BYTES,
              "ExposePayload exceeds ACTION_PAYLOAD_DEVICE_CLASS_BYTES — raise it via board_overrides.h");

inline ExposePayload& expose_payload(ButtonAction& act) {
    return *reinterpret_cast<ExposePayload*>(act.payload.device_class);
}
inline const ExposePayload& expose_payload(const ButtonAction& act) {
    return *reinterpret_cast<const ExposePayload*>(act.payload.device_class);
}

// ── strip: f-stop test strip sequencer ──────────────────────────────
#define ACTION_TYPE_STRIP      "strip"
#define STRIP_CMD_MAX_LEN      20
#define STRIP_VALUE_MAX_LEN    16

struct StripPayload {
    char command[STRIP_CMD_MAX_LEN];
    char value[STRIP_VALUE_MAX_LEN];
};
static_assert(sizeof(StripPayload) <= ACTION_PAYLOAD_DEVICE_CLASS_BYTES,
              "StripPayload exceeds ACTION_PAYLOAD_DEVICE_CLASS_BYTES — raise it via board_overrides.h");

inline StripPayload& strip_payload(ButtonAction& act) {
    return *reinterpret_cast<StripPayload*>(act.payload.device_class);
}
inline const StripPayload& strip_payload(const ButtonAction& act) {
    return *reinterpret_cast<const StripPayload*>(act.payload.device_class);
}

// ── meter: light metering (SBR → grade) ─────────────────────────────
#define ACTION_TYPE_METER      "meter"
#define METER_CMD_MAX_LEN      20
#define METER_VALUE_MAX_LEN    16

struct MeterPayload {
    char command[METER_CMD_MAX_LEN];
    char value[METER_VALUE_MAX_LEN];
};
static_assert(sizeof(MeterPayload) <= ACTION_PAYLOAD_DEVICE_CLASS_BYTES,
              "MeterPayload exceeds ACTION_PAYLOAD_DEVICE_CLASS_BYTES — raise it via board_overrides.h");

inline MeterPayload& meter_payload(ButtonAction& act) {
    return *reinterpret_cast<MeterPayload*>(act.payload.device_class);
}
inline const MeterPayload& meter_payload(const ButtonAction& act) {
    return *reinterpret_cast<const MeterPayload*>(act.payload.device_class);
}

// ── print: print session log ────────────────────────────────────────
#define ACTION_TYPE_PRINT      "print"
#define PRINT_CMD_MAX_LEN      20
#define PRINT_VALUE_MAX_LEN    16

struct PrintPayload {
    char command[PRINT_CMD_MAX_LEN];
    char value[PRINT_VALUE_MAX_LEN];
};
static_assert(sizeof(PrintPayload) <= ACTION_PAYLOAD_DEVICE_CLASS_BYTES,
              "PrintPayload exceeds ACTION_PAYLOAD_DEVICE_CLASS_BYTES — raise it via board_overrides.h");

inline PrintPayload& print_payload(ButtonAction& act) {
    return *reinterpret_cast<PrintPayload*>(act.payload.device_class);
}
inline const PrintPayload& print_payload(const ButtonAction& act) {
    return *reinterpret_cast<const PrintPayload*>(act.payload.device_class);
}

#endif // IS_DARKROOM_TIMER

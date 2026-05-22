#ifndef BLE_TELEMETRY_H
#define BLE_TELEMETRY_H

// BTHome v2 BLE telemetry advertising (alternative transport to MQTT for
// headless / battery-powered boards). Gated by HAS_BLE.
//
// Data flow:
//   1. Sensor adapters call ble_telemetry_set_<type>(object_id, value) during
//      their normal read cycle.
//   2. ble_telemetry_advertise_burst() builds the BTHome v2 service-data
//      payload from the current values and advertises for N packets at the
//      configured interval, then stops advertising and returns.
//
// In duty-cycle BLE mode the orchestrator (duty_cycle.cpp) sequences this:
//   wake -> sensors_read_all() -> ble_telemetry_advertise_burst() -> deep sleep.
//
// BTHome v2 reference: device info byte 0x40 (v2, unencrypted, no trigger),
// service data UUID 0xFCD2. Object IDs:
//   0x01 battery (uint8,  %)
//   0x02 temperature (sint16, 0.01 C)
//   0x03 humidity (uint16, 0.01 %)
//   0x09 count (uint8)
//   0x0C voltage (uint16, 0.001 V)
//   0x21 motion (binary; uint8 0/1)
//   0x2D window (binary; uint8 0/1)

#include "board_config.h"

#if HAS_BLE

#include <Arduino.h>
#include <stdint.h>

// Initialize NimBLE stack and prepare advertising. Idempotent — safe to call
// multiple times. Must be called from a normal (non-ISR) context after NVS.
void ble_telemetry_init(const char *device_name);

// Tear down advertising and (when no other NimBLE user remains) the stack.
// Safe to call when already stopped.
void ble_telemetry_deinit();

// Set the latest value for a BTHome object id. Values are buffered and
// included in the next advertise burst. Pass the SCALED integer the BTHome
// spec expects (e.g. temperature in 0.01 C units: 23.45 C -> 2345).
//
// Up to BLE_TELEMETRY_MAX_OBJECTS distinct object ids are tracked. Repeated
// calls for the same id overwrite the previous value.
bool ble_telemetry_set_u8(uint8_t object_id, uint8_t value);
bool ble_telemetry_set_u16(uint8_t object_id, uint16_t value);
bool ble_telemetry_set_s16(uint8_t object_id, int16_t value);

// Number of distinct object ids currently buffered (debug / test).
size_t ble_telemetry_pending_count();

// Build the BTHome payload from buffered values and advertise it as a burst.
//   burst_count   number of advertisement packets to emit (1..255). Clamped.
//   interval_ms   advertisement interval (20..10240 ms; clamped).
// Blocks until the burst has completed, then stops advertising. Returns false
// if the BLE stack failed to start or there is nothing to advertise.
bool ble_telemetry_advertise_burst(uint8_t burst_count, uint16_t interval_ms);

#endif // HAS_BLE

#endif // BLE_TELEMETRY_H

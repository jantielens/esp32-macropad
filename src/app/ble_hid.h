#pragma once

#include "board_config.h"
#include <stdint.h>

#if HAS_BLE_HID

// Initialize BLE HID stack (manual HID GATT service, security, advertising).
// Uses device_name for the BLE device name and can optionally start directly
// in pairing mode (clearing bonds and opening a 60-second window).
void ble_hid_init(const char* device_name, bool force_pairing_mode);

// Clear bonds, disconnect, and enter pairing mode (60-second window).
// The old host may keep reconnecting briefly until the user removes the
// device from its Bluetooth settings — this is normal Windows behavior.
// WARNING: Must NOT be called from a PSRAM-stack task (NVS flash ops crash).
//          Use ble_hid_request_pairing() from LVGL instead.
void ble_hid_start_pairing();

// Request pairing — safe to call from any task (deferred to ble_hid_loop).
// Clears bonds and reopens the 60-second pairing window.
void ble_hid_request_pairing();

// Request a key sequence — safe to call from any task (deferred to ble_hid_loop).
// The sequence string is copied internally.
void ble_hid_request_sequence(const char* sequence);

// Process pending BLE HID requests. Call from main loop() (non-PSRAM stack).
void ble_hid_loop();

// Send a keyboard key press + release.
// `usage` is a USB HID keyboard usage code (0x04 = 'a', 0x28 = Enter, etc.).
// `modifiers` is a bitmask of modifier keys (KS_MOD_LCTRL, etc.).
void ble_hid_send_key(uint16_t usage, uint8_t modifiers);

// Send a consumer control key press + release.
// `usage` is a USB HID consumer usage code (0x00E9 = volume up, etc.).
void ble_hid_send_consumer(uint16_t usage);

// Returns true when a BLE HID host is connected.
bool ble_hid_is_connected();

// Returns true when pairing mode is active (accepting new devices).
bool ble_hid_is_pairing();

// Returns the connected peer's OTA Bluetooth address ("XX:XX:XX:XX:XX:XX").
// Empty string when no peer is connected.
const char* ble_hid_peer_addr();

// Returns the current BLE device name advertised by the keyboard.
// Empty string until BLE has been initialized.
const char* ble_hid_name();

// Returns the connected peer's identity address (stable across reconnections).
// Empty string when no peer is connected or encryption hasn't completed.
const char* ble_hid_peer_id_addr();

// Returns true when the connected peer has an active bond.
bool ble_hid_is_bonded();

// Returns true when the connection is encrypted.
bool ble_hid_is_encrypted();

// Returns true when the BLE HID stack has been initialized.
bool ble_hid_is_initialized();

// Returns the detailed BLE state string for diagnostics and advanced bindings.
const char* ble_hid_state();

// Returns a compact user-facing BLE status string.
const char* ble_hid_status();

// Execute a parsed key sequence (called from action dispatch).
// The sequence string is parsed on-the-fly and executed synchronously.
void ble_hid_execute_sequence(const char* sequence);

#endif // HAS_BLE_HID

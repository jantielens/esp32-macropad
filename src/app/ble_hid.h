#pragma once

#include "board_config.h"
#include <stdint.h>

#if HAS_BLE_HID

// Initialize BLE HID stack (manual HID GATT service, security, advertising).
// Uses device_name for the BLE device name and starts advertising immediately.
void ble_hid_init(const char* device_name);

// Clear all bonded devices and start discoverable advertising.
// After a successful pairing, discoverable advertising stops automatically.
// WARNING: Must NOT be called from a PSRAM-stack task (flash ops crash).
//          Use ble_hid_request_pairing() from LVGL instead.
void ble_hid_start_pairing();

// Request pairing — safe to call from any task (deferred to ble_hid_loop).
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

// Returns the connected peer's identity address (stable across reconnections).
// Empty string when no peer is connected or encryption hasn't completed.
const char* ble_hid_peer_id_addr();

// Returns true when the connected peer has an active bond.
bool ble_hid_is_bonded();

// Returns true when the connection is encrypted.
bool ble_hid_is_encrypted();

// Execute a parsed key sequence (called from action dispatch).
// The sequence string is parsed on-the-fly and executed synchronously.
void ble_hid_execute_sequence(const char* sequence);

#endif // HAS_BLE_HID

#pragma once

#include "board_config.h"
#include <stdint.h>

#if HAS_BLE_HID

// Initialize BLE HID stack (server, security, HID service).
// Uses device_name for the BLE device name.
// If a bonded host exists, starts whitelist-only advertising (auto-reconnect).
// Otherwise, does NOT advertise (silent until ble_hid_start_pairing() is called).
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
// Consumer control is currently disabled while BLE HID runs in keyboard-only
// compatibility mode.
void ble_hid_send_consumer(uint16_t usage);

// Returns true when a BLE HID host is connected.
bool ble_hid_is_connected();

// Execute a parsed key sequence (called from action dispatch).
// The sequence string is parsed on-the-fly and executed synchronously.
void ble_hid_execute_sequence(const char* sequence);

#endif // HAS_BLE_HID

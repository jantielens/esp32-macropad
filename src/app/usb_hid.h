#pragma once

#include "board_config.h"
#include "key_sequence.h"
#include <stdint.h>

#if HAS_USB_HID
struct MouseHidReport;
struct GamepadReport;
void usb_hid_init(const char* device_name, bool enable_hid);
bool usb_hid_is_ready();
const char* usb_hid_status();
bool usb_hid_gamepad_enabled();
uint32_t usb_hid_epoch();
uint32_t gamepad_hid_submission_generation();
bool usb_hid_send_report(KsUsageType type, uint16_t usage, uint8_t modifiers);
bool usb_hid_send_mouse_report(const MouseHidReport& report, uint32_t expected_epoch,
							  uint32_t expected_ota_epoch);
bool usb_hid_send_gamepad_report(const GamepadReport& report, uint32_t expected_epoch,
                                uint32_t expected_ota_epoch, uint32_t expected_generation);
#endif
#pragma once

#include "board_config.h"
#include "key_sequence.h"
#include <stdint.h>

#if HAS_USB_HID
struct MouseHidReport;
void usb_hid_init(const char* device_name, bool enable_hid);
bool usb_hid_is_ready();
uint32_t usb_hid_epoch();
bool usb_hid_send_report(KsUsageType type, uint16_t usage, uint8_t modifiers);
bool usb_hid_send_mouse_report(const MouseHidReport& report, uint32_t expected_epoch,
							  uint32_t expected_ota_epoch);
#endif
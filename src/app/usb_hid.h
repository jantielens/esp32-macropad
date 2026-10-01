#pragma once

#include "board_config.h"
#include "key_sequence.h"
#include <stdint.h>

#if HAS_USB_HID
void usb_hid_init(const char* device_name, bool enable_hid);
bool usb_hid_is_ready();
uint32_t usb_hid_epoch();
bool usb_hid_send_report(KsUsageType type, uint16_t usage, uint8_t modifiers);
#endif
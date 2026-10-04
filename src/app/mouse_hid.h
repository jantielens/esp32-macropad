#pragma once

#include "mouse_hid_buttons.h"

#include "board_config.h"
#include <stdint.h>

#if HAS_USB_HID
bool mouse_hid_is_ready();
uint32_t mouse_hid_generation();
void mouse_hid_move(int dx, int dy, uint32_t epoch);
void mouse_hid_scroll(int wheel, int pan, uint32_t epoch);
void mouse_hid_begin_touch();
void mouse_hid_start_scroll_inertia(float velocity, float inertia, bool horizontal, uint32_t epoch);
bool mouse_hid_click(uint32_t epoch, uint8_t buttons = mouse_hid_buttons::left);
uint32_t mouse_hid_acquire(uint32_t epoch, uint8_t buttons = mouse_hid_buttons::left);
void mouse_hid_release(uint32_t owner, uint32_t epoch);
void mouse_hid_cancel();
void mouse_hid_loop();
#endif
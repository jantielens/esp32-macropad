#pragma once

#include "board_config.h"
#include "gamepad_hid_state.h"

#if HAS_USB_HID
bool gamepad_hid_is_ready();
uint32_t gamepad_hid_generation();
bool gamepad_hid_hold(GamepadControl control, bool down);
bool gamepad_hid_tap(GamepadControl control, uint32_t continuation_token);
uint32_t gamepad_hid_acquire(GamepadControl control, uint32_t generation);
uint32_t gamepad_hid_acquire_stick(uint8_t stick, uint32_t generation);
bool gamepad_hid_move(uint32_t owner, int16_t horizontal, int16_t vertical, uint32_t generation);
void gamepad_hid_release(uint32_t owner, uint32_t generation);
void gamepad_hid_cancel();
void gamepad_hid_loop();
#endif
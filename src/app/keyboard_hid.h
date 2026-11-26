#pragma once

#include "board_config.h"
#include "keyboard_transport.h"
#include "keyboard_layout.h"

void keyboard_hid_init(KeyboardTransport transport, KeyboardLayout layout = KeyboardLayout::Us);
bool keyboard_hid_request_sequence(const char* sequence, uint32_t continuation_token);
bool keyboard_hid_request_text(const char* text, uint32_t continuation_token, bool allow_controls = false);
void keyboard_hid_cancel();
const char* keyboard_hid_output_status();
uint32_t keyboard_hid_completed_bytes();
uint32_t keyboard_hid_total_bytes();
KeyboardLayout keyboard_hid_layout();
void keyboard_hid_loop();
bool keyboard_hid_is_busy();
bool keyboard_hid_is_ready();
KeyboardTransport keyboard_hid_transport();
bool keyboard_hid_transport_is_ready();
uint32_t keyboard_hid_transport_epoch();
const char* keyboard_hid_status();
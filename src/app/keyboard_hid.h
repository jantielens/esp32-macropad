#pragma once

#include "board_config.h"
#include "keyboard_transport.h"

void keyboard_hid_init(KeyboardTransport transport);
bool keyboard_hid_request_sequence(const char* sequence, uint32_t continuation_token);
void keyboard_hid_loop();
bool keyboard_hid_is_busy();
bool keyboard_hid_is_ready();
KeyboardTransport keyboard_hid_transport();
const char* keyboard_hid_status();
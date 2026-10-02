#pragma once

#include "mouse_hid_buttons.h"

#include "board_config.h"
#include <stdint.h>

#if HAS_USB_HID
#include "mouse_hid_trace.h"

struct MouseTraceTouchSample {
	uint32_t at;
	uint32_t wait_us;
	uint32_t read_us;
	int x;
	int y;
	uint16_t size;
	uint8_t id;
	uint8_t status;
	uint8_t errors;
	uint8_t report[8]{};
	uint8_t post_ack_status = 0;
	uint8_t post_ack_errors = 0;
	uint32_t ack_probe_us = 0;
	bool report_valid = false;
	bool post_ack_checked = false;
	MouseTraceDelayedAck delayed_ack;
};

bool mouse_hid_is_ready();
void mouse_hid_move(int dx, int dy, uint32_t epoch);
void mouse_hid_scroll(int wheel, int pan, uint32_t epoch);
void mouse_hid_begin_touch();
void mouse_hid_start_scroll_inertia(float velocity, float inertia, bool horizontal, uint32_t epoch);
bool mouse_hid_click(uint32_t epoch, uint8_t buttons = mouse_hid_buttons::left);
void mouse_hid_cancel();
void mouse_hid_loop();
void mouse_hid_trace_begin(int x, int y, float threshold, float sensitivity, float acceleration);
void mouse_hid_trace_input(int x, int y, int dx, int dy, bool released);
void mouse_hid_trace_touch(const MouseTraceTouchSample& sample);
bool mouse_hid_trace_touch_is_active();
#endif
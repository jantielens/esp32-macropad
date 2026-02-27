#include "AXS15231B_touch.h"



AXS15231B_Touch* AXS15231B_Touch::instance = nullptr;



bool AXS15231B_Touch::begin() {
		instance = this;

		// Attach interrupt (if valid). Interrupt -> display touched
		int irq = digitalPinToInterrupt(int_pin);
		if (int_pin != 0xFF && irq >= 0) {
				attachInterrupt(irq, isrTouched, FALLING);
				use_interrupt = true;
		} else {
				use_interrupt = false;
		}

		// Start I2C with explicit 400 kHz (matches TOUCH_I2C_FREQ_HZ).
		// Without frequency parameter, Wire defaults to 100 kHz which may
		// be insufficient for AXS15231B to return valid coordinate data.
		return Wire.begin(sda, scl, 400000UL);
}

ISR_PREFIX
void AXS15231B_Touch::isrTouched() {
		// This ISR gets executed if the display reports a touch interrupt
		if (instance) {
				instance->touch_int = true;
		}
}

void AXS15231B_Touch::setRotation(uint8_t rot) {
		rotation = rot;
}

bool AXS15231B_Touch::touched() {
		// Check if the display is touched / got touched
		return update();
}

void AXS15231B_Touch::readData(uint16_t *x, uint16_t *y) {
		// Return the latest data points
		*x = point_X;
		*y = point_Y;
}

void AXS15231B_Touch::enOffsetCorrection(bool en) {
		// Enable offset correction
		en_offset_correction = en;
}

void AXS15231B_Touch::setOffsets(uint16_t x_real_min, uint16_t x_real_max, uint16_t x_ideal_max, uint16_t y_real_min, uint16_t y_real_max, uint16_t y_ideal_max) {
		// Offsets used for offset correction if enabled
		// Offsets should be determinded with rotation = 0
		this->x_real_min = x_real_min;
		this->x_real_max = x_real_max;
		this->y_real_min = y_real_min;
		this->y_real_max = y_real_max;
		this->x_ideal_max = x_ideal_max;
		this->y_ideal_max = y_ideal_max;
}

void AXS15231B_Touch::correctOffset(uint16_t *x, uint16_t *y) {
		// Map values to correct for offset
		*x = map(*x, x_real_min, x_real_max, 0, x_ideal_max);
		*y = map(*y, y_real_min, y_real_max, 0, y_ideal_max);
}

bool AXS15231B_Touch::update() {
		// Check if interrupt occured, if there was an interrupt get data from touch controller and clear flag
		if (use_interrupt) {
				if (!touch_int) {
						return false;
				} else {
						touch_int = false;
				}
		}

		uint8_t tmp_buf[8] = {0};
		// Command to read touch data — matches Espressif's esp_lcd_touch_axs15231b.c
		// 11-byte command: magic + addr + response-length (0x0008) + 3 trailing zeros
		static const uint8_t read_touchpad_cmd[11] = {
				0xB5, 0xAB, 0xA5, 0x5A,
				0x00, 0x00,
				0x00, 0x08,  // response length = 8
				0x00, 0x00, 0x00
		};

		// Send command to controller (STOP, then separate read)
		Wire.beginTransmission(addr);
		Wire.write(read_touchpad_cmd, sizeof(read_touchpad_cmd));
		Wire.endTransmission(true);

		// Small delay to let the controller prepare the response
		delayMicroseconds(100);

		// Read response from controller
		Wire.requestFrom(addr, (uint8_t)sizeof(tmp_buf));
		for(int i = 0; i < sizeof(tmp_buf) && Wire.available(); i++) {
				tmp_buf[i] = Wire.read();
		}

		// Response layout (per Espressif esp_lcd_touch_axs15231b.c):
		//   [0] gesture
		//   [1] num_points (0 = no touch)
		//   [2] event(2b):unused(2b):x_h(4b)   [3] x_l
		//   [4] unused(4b):y_h(4b)              [5] y_l
		uint8_t touch_count = tmp_buf[1];
		uint8_t event = (tmp_buf[2] >> 6) & 0x03;

		// No touch: count is 0
		if (!use_interrupt && touch_count == 0) {
				touchActive = false;
				return false;
		}

		// Invalid touch count (AXS15231B supports max 1 touch point).
		// Garbage frames (e.g. touch_count=255) must also clear the state
		// machine so stale contact(2) events that follow are rejected.
		if (touch_count > 1) {
				touchActive = false;
				return false;
		}

		// Event field state machine:
		//   0 = press down, 1 = lift up, 2 = contact/move, 3 = no event
		// After lift, the controller may replay stale coords with event=2.
		// Require a fresh press(0) before accepting contact(2) events.
		if (event == 0) {
				touchActive = true;
		} else if (event == 1 || event == 3) {
				touchActive = false;
				return false;
		} else if (event == 2 && !touchActive) {
				// Stale contact after lift — ignore
				return false;
		}
		// event==2 && touchActive: valid ongoing touch

		// Extract X and Y coordinates from response
		uint16_t raw_X = AXS_GET_POINT_X(tmp_buf);
		uint16_t raw_Y = AXS_GET_POINT_Y(tmp_buf);

		// Clamp raw coordinates to calibration range.
		// Without clamping, values outside the calibrated area cause
		// correctOffset()'s map() to produce negative (wrapped) results.
		if (raw_X > x_real_max) raw_X = x_real_max;
		if (raw_X < x_real_min) raw_X = x_real_min;
		if (raw_Y > y_real_max) raw_Y = y_real_max;
		if (raw_Y < y_real_min) raw_Y = y_real_min;

		// Correct offset if enabled
		uint16_t x_max, y_max;
		if (en_offset_correction) {
				correctOffset(&raw_X, &raw_Y);
				x_max = x_ideal_max;
				y_max = y_ideal_max;
		} else {
				x_max = x_real_max;
				y_max = y_real_max;
		}

		// Align X and Y according to rotation.
		// These are the *inverse* of the display driver's pixel transpose.
		// Display rot=1: logical(lx,ly) → physical(ly, H-1-lx)
		//   → touch inverse: physical(px,py) → logical(H-1-py, px)
		switch (rotation) {
				case 0:
						point_X = raw_X;
						point_Y = raw_Y;
						break;
				case 1:
						point_X = y_max - raw_Y;
						point_Y = raw_X;
						break;
				case 2:
						point_X = x_max - raw_X;
						point_Y = y_max - raw_Y;
						break;
				case 3:
						point_X = raw_Y;
						point_Y = x_max - raw_X;
						break;
				default:
						break;
		}

		return true;
}

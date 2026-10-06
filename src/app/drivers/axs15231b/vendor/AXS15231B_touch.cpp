#include "AXS15231B_touch.h"
#include "../../../log_manager.h"
#include "../../../touch_sample.h"



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
				instance->touch_int.store(1, std::memory_order_release);
		}
}

void AXS15231B_Touch::setRotation(uint8_t rot) {
		rotation = rot;
}

bool AXS15231B_Touch::touched() {
		// Check if the display is touched / got touched
		return readSample() != ReadStatus::Error && isPressed();
}

AXS15231B_Touch::ReadStatus AXS15231B_Touch::readSample() {
		return update();
}

TouchSnapshot AXS15231B_Touch::readSnapshot() {
		const ReadStatus status = update();
		TouchSnapshot snapshot = last_snapshot;
		snapshot.status = status;
		return snapshot;
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

AXS15231B_Touch::ReadStatus AXS15231B_Touch::update() {
		if (use_interrupt && !touch_int.exchange(0, std::memory_order_acq_rel) && !retry_read && !isPressed())
				return ReadStatus::Unchanged;
		retry_read = true;

		uint8_t tmp_buf[14] = {0};
		const uint8_t response_size = 2 + contact_capacity * 6;
		const auto read_error = [&](const char* stage, unsigned actual, unsigned expected) {
				const uint32_t now = millis();
				if (!error_logged || uint32_t(now - last_error_log_ms) >= 5000) {
						LOGW("AXS15231B", "Read error: stage=%s actual=%u expected=%u report=%02x %02x %02x %02x %02x %02x %02x %02x",
								stage, actual, expected, unsigned(tmp_buf[0]), unsigned(tmp_buf[1]), unsigned(tmp_buf[2]), unsigned(tmp_buf[3]),
								unsigned(tmp_buf[4]), unsigned(tmp_buf[5]), unsigned(tmp_buf[6]), unsigned(tmp_buf[7]));
						error_logged = true;
						last_error_log_ms = now;
				}
				return ReadStatus::Error;
		};
		// Command to read touch data — matches Espressif's esp_lcd_touch_axs15231b.c
		// 11-byte command: magic + addr + response-length + 3 trailing zeros
		const uint8_t read_touchpad_cmd[11] = {
				0xB5, 0xAB, 0xA5, 0x5A,
				0x00, 0x00,
				0x00, response_size,
				0x00, 0x00, 0x00
		};

		// Send command to controller (STOP, then separate read)
		Wire.beginTransmission(addr);
		const size_t written = Wire.write(read_touchpad_cmd, sizeof(read_touchpad_cmd));
		const uint8_t result = Wire.endTransmission(true);
		if (written != sizeof(read_touchpad_cmd)) return read_error("write", written, sizeof(read_touchpad_cmd));
		if (result != 0) return read_error("endTransmission", result, 0);

		// Small delay to let the controller prepare the response
		delayMicroseconds(100);

		// Read response from controller
		const size_t received = Wire.requestFrom(addr, response_size);
		if (received != response_size) return read_error("requestFrom", received, response_size);
		const int available = Wire.available();
		if (available < response_size) return read_error("available", available, response_size);
		for (size_t index = 0; index < response_size; ++index) {
				const int value = Wire.read();
				if (value < 0) return read_error("read", index, response_size);
				tmp_buf[index] = (uint8_t)value;
		}

		const uint8_t touch_count = tmp_buf[1];
		uint16_t previous_ids = 0;
		for (uint8_t index = 0; index < last_snapshot.count; ++index)
				previous_ids |= touch_contact_id_mask(last_snapshot.contacts[index].id);
		TouchSnapshot next;
		uint16_t seen_ids = 0;
		uint16_t next_ids = 0;
		bool stale_move = false;
		bool valid = tmp_buf[0] == 0 && touch_count <= contact_capacity;
		for (uint8_t index = 0; valid && index < touch_count; ++index) {
				const uint8_t* record = tmp_buf + 2 + index * 6;
				const uint8_t event = record[0] >> 6;
				const uint8_t id = record[2] >> 4;
				const uint16_t mask = touch_contact_id_mask(id);
				if (id == 15 || (record[0] & 0x30) || (seen_ids & mask)) {
						valid = false;
						break;
				}
				seen_ids |= mask;
				if (event == 1 || event == 3) continue;
				if (event == 2 && !(previous_ids & mask)) {
						stale_move = true;
						continue;
				}
				TouchContact& contact = next.contacts[next.count++];
				contact.id = id;
				contact.horizontal = ((record[0] & 0x0f) << 8) | record[1];
				contact.vertical = ((record[2] & 0x0f) << 8) | record[3];
				transform(contact.horizontal, contact.vertical);
				next_ids |= mask;
		}

		if (!valid && isPressed()) {
				const uint32_t now = millis();
				if (!invalid_report_active) {
						invalid_report_active = true;
						invalid_report_started_ms = now;
				}
				if (!invalid_report_logged || uint32_t(now - last_invalid_report_log_ms) >= 5000) {
						LOGT("AXS15231B", "Invalid active report: count=%u report=%02x %02x %02x %02x %02x %02x %02x %02x",
								unsigned(touch_count), unsigned(tmp_buf[0]), unsigned(tmp_buf[1]),
								unsigned(tmp_buf[2]), unsigned(tmp_buf[3]), unsigned(tmp_buf[4]), unsigned(tmp_buf[5]),
								unsigned(tmp_buf[6]), unsigned(tmp_buf[7]));
						invalid_report_logged = true;
						last_invalid_report_log_ms = now;
				}
				if (uint32_t(now - invalid_report_started_ms) >= TouchReadFilterState::error_timeout_ms) {
						last_snapshot.count = 0;
				}
				return ReadStatus::Error;
		}
		invalid_report_active = false;
		retry_read = false;
		if (!valid) return ReadStatus::Fresh;
		if (stale_move && !next.count && !last_snapshot.count) return ReadStatus::Unchanged;
		if (next.count) {
				uint8_t primary = 0;
				if (last_snapshot.count) {
						for (uint8_t index = 0; index < next.count; ++index)
								if (next.contacts[index].id == last_snapshot.contacts[0].id) primary = index;
				}
				if (primary) {
						const TouchContact first = next.contacts[0];
						next.contacts[0] = next.contacts[primary];
						next.contacts[primary] = first;
				}
				point_X = next.contacts[0].horizontal;
				point_Y = next.contacts[0].vertical;
		}
		last_snapshot = next;
		if (next_ids != previous_ids)
				LOGT("AXS15231B", "Contacts=%u IDs=0x%04x", unsigned(next.count), unsigned(next_ids));
		return ReadStatus::Fresh;
}

void AXS15231B_Touch::transform(uint16_t& raw_X, uint16_t& raw_Y) {
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
						break;
				case 1: {
						const uint16_t horizontal = y_max - raw_Y;
						raw_Y = raw_X;
						raw_X = horizontal;
						break;
				}
				case 2:
						raw_X = x_max - raw_X;
						raw_Y = y_max - raw_Y;
						break;
				case 3: {
						const uint16_t vertical = x_max - raw_X;
						raw_X = raw_Y;
						raw_Y = vertical;
						break;
				}
				default:
						break;
		}
}

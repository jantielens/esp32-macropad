#ifndef AXS15231B_Touch_H
#define AXS15231B_Touch_H

#include "Arduino.h"
#include "Wire.h"
#include "../../../touch_sample.h"
#include <atomic>

class AXS15231B_Touch {
private:
		uint8_t scl, sda, int_pin, addr, rotation;

		bool use_interrupt = true;
		bool error_logged = false;
		uint32_t last_error_log_ms = 0;
		bool invalid_report_active = false;
		uint32_t invalid_report_started_ms = 0;
		bool invalid_report_logged = false;
		uint32_t last_invalid_report_log_ms = 0;
		uint8_t contact_capacity = 1;
		TouchSnapshot last_snapshot;

		std::atomic<uint32_t> touch_int{0};
		bool retry_read = false;
		static AXS15231B_Touch* instance;

		uint16_t point_X = 0;
		uint16_t point_Y = 0;

		bool en_offset_correction = false;

		uint16_t x_real_min = 0;
		uint16_t x_real_max = 0;
		uint16_t y_real_min = 0;
		uint16_t y_real_max = 0;
		uint16_t x_ideal_max = 0;
		uint16_t y_ideal_max = 0;

public:
		using ReadStatus = TouchReadStatus;
		ReadStatus readSample();
		TouchSnapshot readSnapshot();
		bool isPressed() const { return last_snapshot.count != 0; }
		AXS15231B_Touch(uint8_t scl, uint8_t sda, uint8_t int_pin, uint8_t addr, uint8_t rotation, uint8_t capacity = 1) {
				this->scl = scl;
				this->sda = sda;
				this->int_pin = int_pin;
				this->addr = addr;
				this->rotation = rotation;
				contact_capacity = capacity == 2 ? 2 : 1;
		}

		bool begin();
		void setRotation(uint8_t rot);

		bool touched();
		void readData(uint16_t *x, uint16_t *y);

		void enOffsetCorrection(bool en);
		void setOffsets(uint16_t x_real_min, uint16_t x_real_max, uint16_t x_ideal_max, uint16_t y_real_min, uint16_t y_real_max, uint16_t y_ideal_max);

private:
		static void isrTouched();
		void correctOffset(uint16_t *x, uint16_t *y);
		void transform(uint16_t& horizontal, uint16_t& vertical);
		ReadStatus update();
};

// Response layout (per Espressif esp_lcd_touch_axs15231b.c):
//   [0] gesture, [1] num_points,
//   [2] event(2b):unused(2b):x_h(4b), [3] x_l,
//   [4] tracking_id(4b):y_h(4b), [5] y_l; records repeat every 6 bytes.
// X: bytes 2 (low 4 bits) + 3
#define AXS_GET_POINT_X(buf) (((buf[2] & 0x0F) << 8) | buf[3])
// Y: bytes 4 (low 4 bits) + 5
#define AXS_GET_POINT_Y(buf) (((buf[4] & 0x0F) << 8) | buf[5])

// Some vendor samples define ISR_ATTR / IRAM_ATTR wrappers; keep compatible.
#ifndef ISR_PREFIX
#define ISR_PREFIX IRAM_ATTR
#endif

#endif

#ifndef AXS15231B_Touch_H
#define AXS15231B_Touch_H

#include "Arduino.h"
#include "Wire.h"

class AXS15231B_Touch {
private:
		uint8_t scl, sda, int_pin, addr, rotation;

		bool use_interrupt = true;

		volatile bool touch_int = false;
		static AXS15231B_Touch* instance;

		uint16_t point_X = 0;
		uint16_t point_Y = 0;

		bool touchActive = false;  // State machine: true after press(0), false after lift(1)

		bool en_offset_correction = false;

		uint16_t x_real_min = 0;
		uint16_t x_real_max = 0;
		uint16_t y_real_min = 0;
		uint16_t y_real_max = 0;
		uint16_t x_ideal_max = 0;
		uint16_t y_ideal_max = 0;

public:
		AXS15231B_Touch(uint8_t scl, uint8_t sda, uint8_t int_pin, uint8_t addr, uint8_t rotation) {
				this->scl = scl;
				this->sda = sda;
				this->int_pin = int_pin;
				this->addr = addr;
				this->rotation = rotation;
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
		bool update();
};

// Response layout (per Espressif esp_lcd_touch_axs15231b.c):
//   [0] gesture, [1] num_points,
//   [2] event(2b):unused(2b):x_h(4b), [3] x_l,
//   [4] unused(4b):y_h(4b), [5] y_l
// X: bytes 2 (low 4 bits) + 3
#define AXS_GET_POINT_X(buf) (((buf[2] & 0x0F) << 8) | buf[3])
// Y: bytes 4 (low 4 bits) + 5
#define AXS_GET_POINT_Y(buf) (((buf[4] & 0x0F) << 8) | buf[5])

// Some vendor samples define ISR_ATTR / IRAM_ATTR wrappers; keep compatible.
#ifndef ISR_PREFIX
#define ISR_PREFIX IRAM_ATTR
#endif

#endif

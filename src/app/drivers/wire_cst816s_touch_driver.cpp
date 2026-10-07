/*
 * Wire CST816S Touch Driver Implementation
 *
 * I2C touch driver using Arduino Wire.h for CST816S.
 * Reads touch coordinates from register 0x02 (5 bytes).
 */

#include "wire_cst816s_touch_driver.h"
#include "../board_config.h"
#include "../log_manager.h"

#define CST816S_I2C_ADDR 0x15
#define CST816S_REG_TOUCH 0x02

Wire_CST816S_TouchDriver::Wire_CST816S_TouchDriver()
		: wire(nullptr), rotation(0), calibrationEnabled(false),
			calXMin(0), calXMax(0), calYMin(0), calYMax(0) {}

Wire_CST816S_TouchDriver::~Wire_CST816S_TouchDriver() {
		// Wire is a global singleton — don't delete it.
		wire = nullptr;
}

void Wire_CST816S_TouchDriver::init() {
		LOGT("CST816S", "Initializing touch (Wire I2C)");

		// Hardware reset
		#ifdef TOUCH_RST
		#if TOUCH_RST >= 0
		pinMode(TOUCH_RST, OUTPUT);
		digitalWrite(TOUCH_RST, LOW);
		delay(10);
		digitalWrite(TOUCH_RST, HIGH);
		delay(50);
		LOGT("CST816S", "Hardware reset via GPIO%d", TOUCH_RST);
		#endif
		#endif

		// Initialize I2C bus
		wire = &Wire;
		#if defined(TOUCH_I2C_SDA) && defined(TOUCH_I2C_SCL)
		const bool bus_ready = wire->begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL, 400000);
		LOGT("CST816S", "I2C init: SDA=%d, SCL=%d, 400kHz", TOUCH_I2C_SDA, TOUCH_I2C_SCL);
		#else
		const bool bus_ready = wire->begin();
		LOGT("CST816S", "I2C init: default pins, default freq");
		#endif

		if (!bus_ready) {
				wire = nullptr;
				LOGE("CST816S", "I2C initialization failed");
				return;
		}

		// Verify the chip responds
		wire->beginTransmission(CST816S_I2C_ADDR);
		uint8_t err = wire->endTransmission();
		if (err == 0) {
				LOGT("CST816S", "Touch controller found at 0x%02X", CST816S_I2C_ADDR);
		} else {
				LOGW("CST816S", "Touch controller not found at 0x%02X (err=%d)", CST816S_I2C_ADDR, err);
		}

		// Disable auto-sleep so the chip stays in active polling mode.
		// Without this, the CST816S sleeps after ~5s of no touch and
		// stops responding to I2C reads until a touch interrupt fires.
		wire->beginTransmission(CST816S_I2C_ADDR);
		const size_t register_written = wire->write(0xFE);
		const size_t value_written = wire->write(0x01);
		const uint8_t sleep_result = wire->endTransmission();
		if (register_written != 1 || value_written != 1 || sleep_result != 0) {
				wire = nullptr;
				LOGW("CST816S", "Failed to disable auto-sleep (err=%d)", sleep_result);
		} else {
				LOGT("CST816S", "Init complete (auto-sleep disabled)");
		}
}

TouchSample Wire_CST816S_TouchDriver::readTouchRaw() {
		TouchSample sample;
		sample.status = TouchReadStatus::Error;
		if (!wire) return sample;

		// Read touch registers: 5 bytes starting at 0x02
		//   reg 0x02: numPoints (0 or 1)
		//   reg 0x03: event[7:6] | xH[3:0]
		//   reg 0x04: xL[7:0]
		//   reg 0x05: touchID[7:4] | yH[3:0]
		//   reg 0x06: yL[7:0]
		wire->beginTransmission(CST816S_I2C_ADDR);
		const size_t written = wire->write(CST816S_REG_TOUCH);
		const uint8_t result = wire->endTransmission(false);
		if (written != 1 || result != 0) return sample;
		uint8_t data[5];
		if (wire->requestFrom((uint8_t)CST816S_I2C_ADDR, (uint8_t)sizeof(data)) != sizeof(data) ||
				wire->available() < (int)sizeof(data)) return sample;
		for (size_t index = 0; index < sizeof(data); ++index) {
				const int value = wire->read();
				if (value < 0) return sample;
				data[index] = (uint8_t)value;
		}
		const uint8_t event = data[1] >> 6;
		if (data[0] > 1 || (data[0] && event == 3)) return sample;
		sample.status = TouchReadStatus::Fresh;
		sample.pressed = data[0] != 0 && event != 1;
		if (sample.pressed) {
				sample.horizontal = (uint16_t)(((data[1] & 0x0F) << 8) | data[2]);
				sample.vertical = (uint16_t)(((data[3] & 0x0F) << 8) | data[4]);
		}
		return sample;
}

bool Wire_CST816S_TouchDriver::isTouched() {
		const TouchSample sample = readSample();
		return sample.status != TouchReadStatus::Error && sample.pressed;
}

bool Wire_CST816S_TouchDriver::getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) {
		if (pressure) *pressure = 0;
		if (!x || !y) return false;

		const TouchSample sample = readSample();
		if (sample.status == TouchReadStatus::Error || !sample.pressed) return false;
		*x = sample.horizontal;
		*y = sample.vertical;
		return true;
}

TouchSample Wire_CST816S_TouchDriver::readSample() {
		TouchSample sample = readTouchRaw();
		if (sample.status == TouchReadStatus::Error || !sample.pressed) return sample;
		uint16_t tx = sample.horizontal, ty = sample.vertical;

		// Apply calibration mapping
		if (calibrationEnabled && calXMax > calXMin && calYMax > calYMin) {
				uint32_t cx = tx;
				uint32_t cy = ty;
				if (cx < calXMin) cx = calXMin;
				if (cx > calXMax) cx = calXMax;
				if (cy < calYMin) cy = calYMin;
				if (cy > calYMax) cy = calYMax;

				tx = (uint16_t)((cx - calXMin) * (uint32_t)(DISPLAY_WIDTH - 1) / (uint32_t)(calXMax - calXMin));
				ty = (uint16_t)((cy - calYMin) * (uint32_t)(DISPLAY_HEIGHT - 1) / (uint32_t)(calYMax - calYMin));
		}

		applyRotation(tx, ty);

		sample.horizontal = tx;
		sample.vertical = ty;
		return sample;
}

void Wire_CST816S_TouchDriver::setCalibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) {
		calibrationEnabled = true;
		calXMin = x_min;
		calXMax = x_max;
		calYMin = y_min;
		calYMax = y_max;
}

void Wire_CST816S_TouchDriver::setRotation(uint8_t r) {
		rotation = r & 0x03;
}

void Wire_CST816S_TouchDriver::applyRotation(uint16_t& x, uint16_t& y) const {
		uint16_t w = DISPLAY_WIDTH;
		uint16_t h = DISPLAY_HEIGHT;

		switch (rotation) {
				case 0:
						return;
				case 1: {
						uint16_t nx = y;
						uint16_t ny = (uint16_t)(w - 1 - x);
						x = nx;
						y = ny;
						return;
				}
				case 2:
						x = (uint16_t)(w - 1 - x);
						y = (uint16_t)(h - 1 - y);
						return;
				case 3: {
						uint16_t nx = (uint16_t)(h - 1 - y);
						uint16_t ny = x;
						x = nx;
						y = ny;
						return;
				}
				default:
						return;
		}
}

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
		LOGI("CST816S", "Initializing touch (Wire I2C)");

		// Hardware reset
		#ifdef TOUCH_RST
		#if TOUCH_RST >= 0
		pinMode(TOUCH_RST, OUTPUT);
		digitalWrite(TOUCH_RST, LOW);
		delay(10);
		digitalWrite(TOUCH_RST, HIGH);
		delay(50);
		LOGI("CST816S", "Hardware reset via GPIO%d", TOUCH_RST);
		#endif
		#endif

		// Initialize I2C bus
		wire = &Wire;
		#if defined(TOUCH_I2C_SDA) && defined(TOUCH_I2C_SCL)
		wire->begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL, 400000);
		LOGI("CST816S", "I2C init: SDA=%d, SCL=%d, 400kHz", TOUCH_I2C_SDA, TOUCH_I2C_SCL);
		#else
		wire->begin();
		LOGI("CST816S", "I2C init: default pins, default freq");
		#endif

		// Verify the chip responds
		wire->beginTransmission(CST816S_I2C_ADDR);
		uint8_t err = wire->endTransmission();
		if (err == 0) {
				LOGI("CST816S", "Touch controller found at 0x%02X", CST816S_I2C_ADDR);
		} else {
				LOGW("CST816S", "Touch controller not found at 0x%02X (err=%d)", CST816S_I2C_ADDR, err);
		}

		// Disable auto-sleep so the chip stays in active polling mode.
		// Without this, the CST816S sleeps after ~5s of no touch and
		// stops responding to I2C reads until a touch interrupt fires.
		wire->beginTransmission(CST816S_I2C_ADDR);
		wire->write(0xFE);   // DisAutoSleep register
		wire->write(0x01);   // 1 = disable auto-sleep
		wire->endTransmission();

		LOGI("CST816S", "Init complete (auto-sleep disabled)");
}

bool Wire_CST816S_TouchDriver::readTouchRaw(uint16_t& x, uint16_t& y) {
		if (!wire) return false;

		// Read touch registers: 5 bytes starting at 0x02
		//   reg 0x02: numPoints (0 or 1)
		//   reg 0x03: event[7:6] | xH[3:0]
		//   reg 0x04: xL[7:0]
		//   reg 0x05: touchID[7:4] | yH[3:0]
		//   reg 0x06: yL[7:0]
		wire->beginTransmission(CST816S_I2C_ADDR);
		wire->write(CST816S_REG_TOUCH);
		if (wire->endTransmission(false) != 0) return false;

		if (wire->requestFrom((uint8_t)CST816S_I2C_ADDR, (uint8_t)5) != 5) return false;

		uint8_t numPoints = wire->read();  // 0x02: number of touch points
		uint8_t xH        = wire->read();  // 0x03: event[7:6] | x[11:8]
		uint8_t xL        = wire->read();  // 0x04: x[7:0]
		uint8_t yH        = wire->read();  // 0x05: touchID[7:4] | y[11:8]
		uint8_t yL        = wire->read();  // 0x06: y[7:0]

		if (numPoints == 0) return false;

		x = (uint16_t)(((xH & 0x0F) << 8) | xL);
		y = (uint16_t)(((yH & 0x0F) << 8) | yL);
		return true;
}

bool Wire_CST816S_TouchDriver::isTouched() {
		uint16_t x, y;
		return readTouchRaw(x, y);
}

bool Wire_CST816S_TouchDriver::getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) {
		if (pressure) *pressure = 0;
		if (!x || !y) return false;

		uint16_t tx, ty;
		if (!readTouchRaw(tx, ty)) return false;

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

		*x = tx;
		*y = ty;
		return true;
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

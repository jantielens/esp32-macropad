/*
 * Wire CST816S Touch Driver
 *
 * Capacitive touch driver for CST816S over Arduino Wire (I2C).
 * Replaces the ESP_Panel-based driver, eliminating the ESP_Panel
 * library dependency and its legacy I2C driver conflict.
 *
 * Protocol: I2C address 0x15, register 0x02, 5 bytes
 *   [numPoints, event|xH, xL, touchID|yH, yL]
 *   x = ((xH & 0x0F) << 8) | xL
 *   y = ((yH & 0x0F) << 8) | yL
 *   event = xH >> 6  (0=DOWN, 1=UP, 2=CONTACT)
 */

#ifndef WIRE_CST816S_TOUCH_DRIVER_H
#define WIRE_CST816S_TOUCH_DRIVER_H

#include "../touch_driver.h"
#include <Wire.h>

class Wire_CST816S_TouchDriver : public TouchDriver {
public:
		Wire_CST816S_TouchDriver();
		~Wire_CST816S_TouchDriver() override;

		void init() override;

		bool isTouched() override;
		bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) override;

		void setCalibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) override;
		void setRotation(uint8_t rotation) override;

private:
		TwoWire* wire;
		uint8_t rotation;

		bool calibrationEnabled;
		uint16_t calXMin, calXMax;
		uint16_t calYMin, calYMax;

		void applyRotation(uint16_t& x, uint16_t& y) const;
		bool readTouchRaw(uint16_t& x, uint16_t& y);
};

#endif // WIRE_CST816S_TOUCH_DRIVER_H

#ifndef INKPLATE6FLICK_TOUCH_DRIVER_H
#define INKPLATE6FLICK_TOUCH_DRIVER_H

#include "../touch_driver.h"

#include <freertos/semphr.h>

class Inkplate6Flick_TouchDriver : public TouchDriver {
public:
		Inkplate6Flick_TouchDriver();
		~Inkplate6Flick_TouchDriver() override;

		void init() override;
		bool isTouched() override;
		bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) override;
		void setCalibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) override;
		void setRotation(uint8_t rotation) override;

private:
		bool initialized;
		bool touched;
		uint16_t lastX;
		uint16_t lastY;
		uint8_t rotation;
		SemaphoreHandle_t mutex;
};

#endif
#include "inkplate6flick_touch_driver.h"

#include "inkplate6flick_lvgl_driver.h"
#include "../log_manager.h"

#include <Inkplate.h>

Inkplate6Flick_TouchDriver::Inkplate6Flick_TouchDriver()
		: initialized(false), lastX(0), lastY(0) {}

void Inkplate6Flick_TouchDriver::init() {
		Inkplate* display = inkplate6flick_lvgl_instance();
		if (!display) {
			LOGE("InkplateTouch", "display is unavailable");
			return;
		}
		initialized = display->touchscreen.init(true);
		LOGI("InkplateTouch", "Cypress touch %s", initialized ? "initialized" : "initialization failed");
}

bool Inkplate6Flick_TouchDriver::isTouched() {
		uint16_t x = 0;
		uint16_t y = 0;
		return getTouch(&x, &y);
}

bool Inkplate6Flick_TouchDriver::getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) {
		if (pressure) *pressure = 0;
		if (!initialized || !x || !y) return false;

		Inkplate* display = inkplate6flick_lvgl_instance();
		if (!display) return false;
		uint16_t touchX[2] = {};
		uint16_t touchY[2] = {};
		if (display->touchscreen.getData(touchX, touchY) == 0) return false;

		lastX = touchX[0];
		lastY = touchY[0];
		*x = lastX;
		*y = lastY;
		return true;
}

void Inkplate6Flick_TouchDriver::setCalibration(uint16_t, uint16_t, uint16_t, uint16_t) {}
void Inkplate6Flick_TouchDriver::setRotation(uint8_t) {}
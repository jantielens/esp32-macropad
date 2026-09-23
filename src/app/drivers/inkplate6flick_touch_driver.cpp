#include "inkplate6flick_touch_driver.h"

#include "inkplate6flick_epaper_driver.h"
#include "../log_manager.h"

#include <Inkplate.h>

Inkplate6Flick_TouchDriver::Inkplate6Flick_TouchDriver()
		: initialized(false), touched(false), lastX(0), lastY(0), rotation(0), mutex(nullptr) {}

Inkplate6Flick_TouchDriver::~Inkplate6Flick_TouchDriver() {
		if (mutex) {
			vSemaphoreDelete(mutex);
		}
}

void Inkplate6Flick_TouchDriver::init() {
		mutex = xSemaphoreCreateMutex();
		if (!mutex) {
			LOGE("InkplateTouch", "touch mutex allocation failed");
			return;
		}

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
		if (!initialized || !x || !y || !mutex) return false;
		if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) return false;

		Inkplate* display = inkplate6flick_lvgl_instance();
		if (!display) {
			xSemaphoreGive(mutex);
			return false;
		}

		// Cypress reports are interrupt-driven: getData() returns zero both for
		// an explicit release and when no new report is pending. Preserve the last
		// reported state until the controller sends the next report.
		if (display->touchscreen.available()) {
			uint16_t touchX[2] = {};
			uint16_t touchY[2] = {};
			const uint8_t touchCount = display->touchscreen.getData(touchX, touchY);
			touched = touchCount > 0;
			if (touched) {
				lastX = touchX[0];
				lastY = touchY[0];
			}
		}

		switch (rotation) {
				case 1: *x = lastY; *y = DISPLAY_WIDTH - 1 - lastX; break;
				case 2: *x = DISPLAY_WIDTH - 1 - lastX; *y = DISPLAY_HEIGHT - 1 - lastY; break;
				case 3: *x = DISPLAY_HEIGHT - 1 - lastY; *y = lastX; break;
				default: *x = lastX; *y = lastY; break;
		}
		const bool isTouched = touched;
		xSemaphoreGive(mutex);
		return isTouched;
}

void Inkplate6Flick_TouchDriver::setCalibration(uint16_t, uint16_t, uint16_t, uint16_t) {}
void Inkplate6Flick_TouchDriver::setRotation(uint8_t value) { rotation = value & 3; }
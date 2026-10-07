#include "xpt2046_driver.h"
#include "../log_manager.h"

XPT2046_Driver::XPT2046_Driver(uint8_t cs, uint8_t irq) 
		: ts(cs, irq), touchSPI(nullptr), cs_pin(cs), irq_pin(irq), rotation(1) {
		// Default calibration values (will be overridden by board config)
		cal_x_min = 300;
		cal_x_max = 3900;
		cal_y_min = 200;
		cal_y_max = 3700;
}

XPT2046_Driver::~XPT2046_Driver() {
		if (touchSPI) {
				delete touchSPI;
				touchSPI = nullptr;
		}
}

void XPT2046_Driver::init() {
		LOGT("XPT2046", "Initializing (CS=%d, IRQ=%d)", cs_pin, irq_pin);
		lastSample = TouchSample{};
		lastPressure = 0;
		
		// Configure SPI bus for touch controller (CYD uses separate VSPI bus)
		#if defined(TOUCH_MOSI) && defined(TOUCH_MISO) && defined(TOUCH_SCLK)
		touchSPI = new SPIClass(VSPI);
		touchSPI->begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
		LOGT("XPT2046", "SPI bus configured (MOSI=%d, MISO=%d, CLK=%d, CS=%d)",
									 TOUCH_MOSI, TOUCH_MISO, TOUCH_SCLK, TOUCH_CS);
		
		// Initialize XPT2046 touchscreen library with custom SPI
		initialized = ts.begin(*touchSPI);
		#else
		// Use default SPI bus
		initialized = ts.begin();
		#endif
		if (!initialized) {
				LOGE("XPT2046", "Touch controller initialization failed");
				return;
		}
		
		ts.setRotation(rotation);
		
		LOGT("XPT2046", "Calibration (%d,%d) to (%d,%d), rotation=%d",
									 cal_x_min, cal_y_min, cal_x_max, cal_y_max, rotation);
		LOGT("XPT2046", "Initialization complete");
}

bool XPT2046_Driver::isTouched() {
		const TouchSample sample = readSample();
		return sample.status != TouchReadStatus::Error && sample.pressed;
}

bool XPT2046_Driver::getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) {
		if (pressure) *pressure = 0;
		if (!x || !y) return false;
		const TouchSample sample = readSample();
		if (sample.status == TouchReadStatus::Error || !sample.pressed) return false;
		*x = sample.horizontal;
		*y = sample.vertical;
		if (pressure) *pressure = lastPressure;
		return true;
}

TouchSample XPT2046_Driver::readSample() {
		TouchSample sample = lastSample;
		sample.status = TouchReadStatus::Fresh;
		if (!initialized) {
				sample.status = TouchReadStatus::Error;
				return sample;
		}
		if (!ts.tirqTouched()) {
				sample.pressed = false;
				lastPressure = 0;
				lastSample = sample;
				return sample;
		}
		if (ts.bufferEmpty()) {
				sample.status = TouchReadStatus::Unchanged;
				return sample;
		}
		TS_Point p = ts.getPoint();
		if (p.x < 0 || p.y < 0 || p.z < 0 || p.x >= 8000 || p.y >= 8000 || p.z >= 4000) {
				sample.status = TouchReadStatus::Error;
				return sample;
		}
		if (p.z < 200) {
				sample.pressed = false;
				lastPressure = 0;
				lastSample = sample;
				return sample;
		}
		
		// Map raw coordinates (0-4095) to calibrated screen coordinates
		int32_t mapped_x = map(p.x, cal_x_min, cal_x_max, 0, DISPLAY_WIDTH - 1);
		int32_t mapped_y = map(p.y, cal_y_min, cal_y_max, 0, DISPLAY_HEIGHT - 1);
		
		// Clamp to display bounds
		sample.horizontal = constrain(mapped_x, 0, DISPLAY_WIDTH - 1);
		sample.vertical = constrain(mapped_y, 0, DISPLAY_HEIGHT - 1);
		sample.pressed = true;
		lastPressure = p.z;
		lastSample = sample;
		return sample;
}

void XPT2046_Driver::setCalibration(uint16_t x_min, uint16_t x_max, 
																		 uint16_t y_min, uint16_t y_max) {
		cal_x_min = x_min;
		cal_x_max = x_max;
		cal_y_min = y_min;
		cal_y_max = y_max;
		
		LOGT("XPT2046", "Calibration updated (%d,%d) to (%d,%d)",
									 cal_x_min, cal_y_min, cal_x_max, cal_y_max);
}

void XPT2046_Driver::setRotation(uint8_t rot) {
		rotation = rot;
		ts.setRotation(rotation);
		
		LOGT("XPT2046", "Rotation set to %d", rotation);
}

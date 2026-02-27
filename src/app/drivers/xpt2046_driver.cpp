#include "xpt2046_driver.h"
#include "../log_manager.h"

XPT2046_Driver::XPT2046_Driver(uint8_t cs, uint8_t irq) 
		: ts(cs, irq), cs_pin(cs), irq_pin(irq), rotation(1), touchSPI(nullptr) {
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
		LOGI("XPT2046", "Initializing (CS=%d, IRQ=%d)", cs_pin, irq_pin);
		
		// Configure SPI bus for touch controller (CYD uses separate VSPI bus)
		#if defined(TOUCH_MOSI) && defined(TOUCH_MISO) && defined(TOUCH_SCLK)
		touchSPI = new SPIClass(VSPI);
		touchSPI->begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
		LOGI("XPT2046", "SPI bus configured (MOSI=%d, MISO=%d, CLK=%d, CS=%d)", 
									 TOUCH_MOSI, TOUCH_MISO, TOUCH_SCLK, TOUCH_CS);
		
		// Initialize XPT2046 touchscreen library with custom SPI
		ts.begin(*touchSPI);
		#else
		// Use default SPI bus
		ts.begin();
		#endif
		
		ts.setRotation(rotation);
		
		LOGI("XPT2046", "Calibration (%d,%d) to (%d,%d), rotation=%d", 
									 cal_x_min, cal_y_min, cal_x_max, cal_y_max, rotation);
		LOGI("XPT2046", "Initialization complete");
}

bool XPT2046_Driver::isTouched() {
		return ts.touched();
}

bool XPT2046_Driver::getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) {
		// Check if screen is being touched
		if (!ts.tirqTouched() && ts.bufferEmpty()) {
				return false;
		}
		
		// Get raw coordinates from XPT2046
		TS_Point p = ts.getPoint();
		
		// Validate raw values (should be in 0-4095 range, not max noise values)
		if (p.x >= 8000 || p.y >= 8000 || p.z >= 4000) {
				// Invalid/noise - touch controller not responding properly
				return false;
		}
		
		// Filter by pressure - XPT2046 needs minimum pressure to be valid touch
		// z=0 is electrical noise, not actual touch
		if (p.z < 200) {  // Minimum pressure threshold
				return false;
		}
		
		// Map raw coordinates (0-4095) to calibrated screen coordinates
		int32_t mapped_x = map(p.x, cal_x_min, cal_x_max, 0, DISPLAY_WIDTH - 1);
		int32_t mapped_y = map(p.y, cal_y_min, cal_y_max, 0, DISPLAY_HEIGHT - 1);
		
		// Clamp to display bounds
		*x = constrain(mapped_x, 0, DISPLAY_WIDTH - 1);
		*y = constrain(mapped_y, 0, DISPLAY_HEIGHT - 1);
		
		// Optionally return pressure (Z coordinate)
		if (pressure) {
				*pressure = p.z;
		}
		
		return true;
}

void XPT2046_Driver::setCalibration(uint16_t x_min, uint16_t x_max, 
																		 uint16_t y_min, uint16_t y_max) {
		cal_x_min = x_min;
		cal_x_max = x_max;
		cal_y_min = y_min;
		cal_y_max = y_max;
		
		LOGI("XPT2046", "Calibration updated (%d,%d) to (%d,%d)", 
									 cal_x_min, cal_y_min, cal_x_max, cal_y_max);
}

void XPT2046_Driver::setRotation(uint8_t rot) {
		rotation = rot;
		ts.setRotation(rotation);
		
		LOGI("XPT2046", "Rotation set to %d", rotation);
}

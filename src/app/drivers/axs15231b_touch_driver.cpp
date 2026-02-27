/*
 * AXS15231B Touch Driver Implementation
 * 
 * Wraps the vendored AXS15231B_Touch class to match our TouchDriver HAL.
 */

#include "axs15231b_touch_driver.h"
#include "../log_manager.h"

// Touch I2C address from sample
#ifndef TOUCH_I2C_ADDR
#define TOUCH_I2C_ADDR 0x3B
#endif

AXS15231B_TouchDriver::AXS15231B_TouchDriver() 
		: touch(nullptr), screenWidth(DISPLAY_WIDTH), screenHeight(DISPLAY_HEIGHT) {
}

AXS15231B_TouchDriver::~AXS15231B_TouchDriver() {
		if (touch) {
				delete touch;
		}
}

void AXS15231B_TouchDriver::init() {
		LOGI("AXS15231B", "Initializing I2C touch controller");
		
		#ifdef TOUCH_I2C_SCL
		// Create touch instance with I2C pins and interrupt
		// From sample: AXS15231B_Touch(SCL, SDA, INT, ADDR, rotation)
		uint8_t int_pin = 3;  // Default from sample
		bool use_polling = false;
		#ifdef TOUCH_INT
		if (TOUCH_INT >= 0) {
				int_pin = TOUCH_INT;
		} else {
				int_pin = 0xFF;
				use_polling = true;
		}
		#endif
		
		touch = new AXS15231B_Touch(
				TOUCH_I2C_SCL,
				TOUCH_I2C_SDA,
				int_pin,
				TOUCH_I2C_ADDR,
				DISPLAY_ROTATION
		);
		
		if (!touch->begin()) {
				LOGE("AXS15231B", "Failed to initialize touch controller");
				return;
		}
		
		// Enable offset correction (from sample)
		touch->enOffsetCorrection(true);
		
		if (use_polling) {
				LOGI("AXS15231B", "Touch controller initialized (polling mode)");
		} else {
				LOGI("AXS15231B", "Touch controller initialized");
		}
		#else
		LOGE("AXS15231B", "Touch I2C pins not defined in board_config.h");
		#endif
}

bool AXS15231B_TouchDriver::isTouched() {
		if (!touch) return false;
		return touch->touched();
}

bool AXS15231B_TouchDriver::getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure) {
		if (!touch) return false;
		
		if (touch->touched()) {
				touch->readData(x, y);
				
				// AXS15231B doesn't provide pressure, set to max if requested
				if (pressure) {
						*pressure = 1000;
				}
				
				return true;
		}
		
		return false;
}

void AXS15231B_TouchDriver::setCalibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) {
		if (!touch) return;
		
		// From sample: setOffsets(x_real_min, x_real_max, x_ideal_max, y_real_min, y_real_max, y_ideal_max)
		// Ideal max values are screen dimensions minus 1
		touch->setOffsets(
				x_min, x_max, screenWidth - 1,
				y_min, y_max, screenHeight - 1
		);
}

void AXS15231B_TouchDriver::setRotation(uint8_t rotation) {
		if (!touch) return;
		touch->setRotation(rotation);
}

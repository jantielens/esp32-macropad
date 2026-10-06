/*
 * AXS15231B Touch Driver
 * 
 * Wrapper for vendored AXS15231B_Touch class (from JC3248W535 sample).
 * Implements TouchDriver HAL interface for touch manager integration.
 */

#ifndef AXS15231B_TOUCH_DRIVER_H
#define AXS15231B_TOUCH_DRIVER_H

#include "../touch_driver.h"
#include "../board_config.h"
#include "axs15231b/vendor/AXS15231B_touch.h"

static_assert(MAX_AXS15231B_CONTACTS == 1 || MAX_AXS15231B_CONTACTS == 2,
		"AXS15231B supports one or two configured contacts");

class AXS15231B_TouchDriver : public TouchDriver {
private:
		AXS15231B_Touch* touch;
		uint16_t screenWidth;
		uint16_t screenHeight;
		bool initialized = false;
		
public:
		AXS15231B_TouchDriver();
		~AXS15231B_TouchDriver() override;
		
		void init() override;
		bool isTouched() override;
		bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) override;
		TouchSample readSample() override;
		TouchSnapshot readSnapshot() override;
		uint8_t contactCapacity() const override { return MAX_AXS15231B_CONTACTS; }
		void setCalibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) override;
		void setRotation(uint8_t rotation) override;
};

#endif // AXS15231B_TOUCH_DRIVER_H

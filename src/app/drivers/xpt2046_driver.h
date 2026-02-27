/*
 * XPT2046 Touch Driver
 * 
 * Standalone driver using XPT2046_Touchscreen library by Paul Stoffregen.
 * Used on ESP32-2432S028R (CYD) and compatible displays.
 * 
 * Hardware: Resistive touch controller on separate VSPI bus
 */

#ifndef XPT2046_DRIVER_H
#define XPT2046_DRIVER_H

#include "../touch_driver.h"
#include "../board_config.h"
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

class XPT2046_Driver : public TouchDriver {
private:
		XPT2046_Touchscreen ts;  // Standalone touch controller
		SPIClass* touchSPI;      // Persistent SPI instance for touch controller
		uint8_t cs_pin;
		uint8_t irq_pin;
		
		// Calibration data
		uint16_t cal_x_min, cal_x_max;
		uint16_t cal_y_min, cal_y_max;
		uint8_t rotation;
		
public:
		// Constructor initializes standalone XPT2046 controller
		XPT2046_Driver(uint8_t cs, uint8_t irq = 255);
		~XPT2046_Driver() override;
		
		void init() override;
		bool isTouched() override;
		bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) override;
		void setCalibration(uint16_t x_min, uint16_t x_max, uint16_t y_min, uint16_t y_max) override;
		void setRotation(uint8_t rotation) override;
};

#endif // XPT2046_DRIVER_H

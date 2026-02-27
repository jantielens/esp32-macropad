/*
 * Touch Driver Interface
 * 
 * Hardware abstraction layer for touch controllers.
 * Allows TouchManager to work with different touch libraries
 * (TFT_eSPI, XPT2046, FT6236, GT911, etc.) through a common interface.
 * 
 * To add a new touch controller:
 * 1. Create a new driver class implementing this interface
 * 2. Add driver selection in board_config.h
 * 3. Include the driver implementation in src/app/touch_drivers.cpp
 */

#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include <Arduino.h>

// ============================================================================
// Touch Driver Interface
// ============================================================================
// Pure virtual interface for touch controller abstraction.
// Minimal set of methods required for LVGL integration.

class TouchDriver {
public:
		virtual ~TouchDriver() = default;
		
		// Hardware initialization
		// Must be called before any touch operations
		virtual void init() = 0;
		
		// Touch detection
		// Returns true if screen is currently being touched
		virtual bool isTouched() = 0;
		
		// Read touch coordinates
		// Parameters:
		//   x, y: Pointers to store touch coordinates (screen space)
		//   pressure: Optional pressure value (nullptr if not needed)
		// Returns: true if valid touch data was read
		virtual bool getTouch(uint16_t* x, uint16_t* y, uint16_t* pressure = nullptr) = 0;
		
		// Calibration settings
		// Set min/max raw values for coordinate mapping
		// These values are typically determined through calibration procedure
		virtual void setCalibration(uint16_t x_min, uint16_t x_max, 
																uint16_t y_min, uint16_t y_max) = 0;
		
		// Rotation handling
		// Set rotation to match display orientation
		// 0=portrait, 1=landscape, 2=portrait_flip, 3=landscape_flip
		virtual void setRotation(uint8_t rotation) = 0;
};

#endif // TOUCH_DRIVER_H

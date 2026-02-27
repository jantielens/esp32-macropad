/*
 * Touch Manager
 * 
 * Manages touch controller lifecycle and LVGL integration.
 * Follows the same pattern as DisplayManager.
 */

#ifndef TOUCH_MANAGER_H
#define TOUCH_MANAGER_H

#include "board_config.h"

#if HAS_TOUCH

#include <Arduino.h>
#include <lvgl.h>
#include "touch_driver.h"

class TouchManager {
private:
		TouchDriver* driver;
		lv_indev_t* indev;

		bool lvglRegisterPending;
		bool tryRegisterWithLVGL();
		
		// LVGL read callback (static, accesses instance via user_data)
		static void readCallback(lv_indev_t* indev, lv_indev_data_t* data);
		
public:
		TouchManager();
		~TouchManager();
		
		// Initialize touch hardware and register with LVGL
		void init();

		// Retry deferred LVGL registration (non-blocking)
		void loop();
		
		// Get touch state (for debugging)
		bool isTouched();
		bool getTouch(uint16_t* x, uint16_t* y);
};

// C-style interface for app.ino
void touch_manager_init();
void touch_manager_loop();
bool touch_manager_is_touched();

// Temporarily suppress LVGL touch input (forces LVGL state=RELEASED).
// Useful to avoid "wake tap" click-through when turning the backlight back on.
void touch_manager_suppress_lvgl_input(uint32_t duration_ms);

// Force LVGL to always see RELEASED while active.
// Screen saver uses this while dimming/asleep/fading in.
void touch_manager_set_lvgl_force_released(bool force_released);

#endif // HAS_TOUCH

#endif // TOUCH_MANAGER_H

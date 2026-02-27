#ifndef FPS_SCREEN_H
#define FPS_SCREEN_H

#include "screen.h"
#include <lvgl.h>

// Forward declaration
class DisplayManager;

// ============================================================================
// FPS Benchmark Screen
// ============================================================================
// Forces continuous full-screen redraws to measure the real panel refresh rate.
// Displays live panel FPS, present() time, and LVGL render time.
// A spinning arc provides visual confirmation that redraws are happening.
// Navigate to/from this screen via the web portal screen API.

class FpsScreen : public Screen {
private:
		lv_obj_t* screen;
		DisplayManager* displayMgr;
		
		// UI elements
		lv_obj_t* fpsValueLabel;
		lv_obj_t* fpsUnitLabel;
		lv_obj_t* presentLabel;
		lv_obj_t* renderLabel;
		lv_obj_t* frameLabel;
		lv_obj_t* arc;
		
		// Arc animation state
		uint16_t arcAngle;
		
public:
		FpsScreen(DisplayManager* manager);
		~FpsScreen();
		
		void create() override;
		void destroy() override;
		void show() override;
		void hide() override;
		void update() override;
};

#endif // FPS_SCREEN_H

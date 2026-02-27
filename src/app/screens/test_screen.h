#ifndef TEST_SCREEN_H
#define TEST_SCREEN_H

#include "screen.h"
#include <lvgl.h>

// Forward declaration
class DisplayManager;

// ============================================================================
// Test Screen
// ============================================================================
// Display calibration and testing screen with color bars and gradients.
// Designed for round 240x240 minimum - gradient centered for maximum width.

class TestScreen : public Screen {
private:
		lv_obj_t* screen;
		DisplayManager* displayMgr;
		
		// UI elements
		lv_obj_t* titleLabel;
		lv_obj_t* redBar;
		lv_obj_t* greenBar;
		lv_obj_t* blueBar;
		lv_obj_t* gradientBar;
		lv_obj_t* yellowBar;
		lv_obj_t* cyanBar;
		lv_obj_t* magentaBar;
		lv_obj_t* infoLabel;
		
		// Touch event handler (static callback)
		static void touchEventCallback(lv_event_t* e);
		
public:
		TestScreen(DisplayManager* manager);
		~TestScreen();
		
		void create() override;
		void destroy() override;
		void show() override;
		void hide() override;
		void update() override;
};

#endif // TEST_SCREEN_H

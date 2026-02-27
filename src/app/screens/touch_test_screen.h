#ifndef TOUCH_TEST_SCREEN_H
#define TOUCH_TEST_SCREEN_H

#include "../board_config.h"

#if HAS_TOUCH

#include "screen.h"
#include <lvgl.h>

// ============================================================================
// Touch Test Screen
// ============================================================================
// Full-screen finger-painting canvas for verifying touch input.
// Draws white circles at each touch point and interpolates between them
// so fast finger movements produce continuous lines.
//
// Resolution-independent: adapts to LVGL logical display dimensions.
//
// Memory: Canvas buffer (~width*height*2 bytes) allocated in PSRAM on show(),
// freed on hide(). Zero PSRAM cost when the screen is not active.
//
// Navigation: Only via web portal (/api/display/screen with "touch_test").
// All touch input goes to drawing — no touch-based exit.
// Canvas is cleared each time the screen is shown.

class TouchTestScreen : public Screen {
private:
		lv_obj_t* screen;
		lv_obj_t* canvas;
	uint8_t* canvasBuf;  // PSRAM-allocated RGB565 buffer, owned by this screen
		// Header label (screen name + resolution)
		lv_obj_t* headerLabel;

		// Previous touch point for line interpolation
		bool prevTouchValid;
		int16_t prevX;
		int16_t prevY;

		// Drawing parameters (resolution-adaptive)
		uint8_t brushRadius;

	// Logical (post-rotation) canvas dimensions, queried from LVGL in create().
	uint16_t canvasWidth;
	uint16_t canvasHeight;

		// Draw a filled circle at (cx, cy)
		void drawDot(int16_t cx, int16_t cy, lv_color_t color, uint8_t radius);

		// Draw interpolated line from (x0,y0) to (x1,y1)
		void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1);

		// LVGL event callback for touch input
		static void touchEventCallback(lv_event_t* e);

public:
		TouchTestScreen();
		~TouchTestScreen();

		void create() override;
		void destroy() override;
		void show() override;
		void hide() override;
		void update() override;
};

#endif // HAS_TOUCH
#endif // TOUCH_TEST_SCREEN_H

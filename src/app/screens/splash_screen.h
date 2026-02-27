#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

#include "screen.h"
#include <lvgl.h>

// ============================================================================
// Splash Screen
// ============================================================================
// Simple boot screen shown during initialization.
// No dependencies - static content only.

class SplashScreen : public Screen {
private:
		lv_obj_t* screen;
		lv_obj_t* logoImg;
		lv_obj_t* statusLabel;
		lv_obj_t* spinner;
		
public:
		SplashScreen();
		~SplashScreen();
		
		void create() override;
		void destroy() override;
		void show() override;
		void hide() override;
		void update() override;
		
		// Update status text (e.g., "Initializing WiFi...")
		void setStatus(const char* text);
};

#endif // SPLASH_SCREEN_H

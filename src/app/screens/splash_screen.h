#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

#include "screen.h"
#include <lvgl.h>
#include <stdint.h>

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
		lv_image_dsc_t customLogoDescriptor;
		uint16_t* customLogoPixels;
		uint8_t fontSize;
		uint32_t textColor;
		uint32_t backgroundColor;
		bool customLogoSelected;
		void releaseCustomLogo();
		
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
		void setAppearance(uint8_t font_size, uint32_t text_color,
				uint32_t background_color, bool custom_logo_selected);
		bool setCustomLogo(uint16_t* pixels, uint16_t width, uint16_t height);
		bool getLogoBounds(uint16_t* width, uint16_t* height) const;
};

#endif // SPLASH_SCREEN_H

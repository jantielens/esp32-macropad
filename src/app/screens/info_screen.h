#ifndef INFO_SCREEN_H
#define INFO_SCREEN_H

#include "screen.h"
#include "../config_manager.h"
#include <lvgl.h>

class DisplayManager;

class InfoScreen : public Screen {
private:
		lv_obj_t* screen;
		DeviceConfig* config;
		DisplayManager* displayMgr;
		lv_obj_t* content;
		lv_obj_t* logoContainer;
		lv_obj_t* logoImage;
		lv_obj_t* deviceNameLabel;
		lv_obj_t* networkLabel;
		lv_obj_t* instructionsLabel;
		lv_obj_t* ipLabel;
		lv_obj_t* mdnsLabel;
		uint32_t lastUpdateMs;
		bool refreshPending;
		uint8_t fontSize;
		uint32_t textColor;
		uint32_t backgroundColor;

		void layout();
		static void touchEventCallback(lv_event_t* e);

public:
		InfoScreen(DeviceConfig* deviceConfig, DisplayManager* manager);
		~InfoScreen() override;
		void setAppearance(uint8_t font_size, uint32_t text_color, uint32_t background_color);
		void setLogo(const lv_image_dsc_t* source);
		void create() override;
		void destroy() override;
		void show() override;
		void hide() override;
		void update() override;
};

#endif

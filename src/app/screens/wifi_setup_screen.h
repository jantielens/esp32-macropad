#ifndef WIFI_SETUP_SCREEN_H
#define WIFI_SETUP_SCREEN_H

#include "screen.h"
#include "../config_manager.h"
#include <lvgl.h>
#include <atomic>

class WifiSetupScreen : public Screen {
private:
		DeviceConfig* config;
		lv_obj_t* screen;
		lv_obj_t* content;
		lv_obj_t* title_label;
		lv_obj_t* logo_container;
		lv_obj_t* logo_image;
		lv_obj_t* ssid_label;
		lv_obj_t* password_label;
		lv_obj_t* url_label;
		lv_obj_t* status_label;
		lv_obj_t* timeout_label;
		lv_obj_t* continue_button;
		lv_obj_t* button_label;
		lv_obj_t* instructions_label;
		lv_obj_t* feedback_label;
		bool details_loaded;
		bool continue_pending;
		std::atomic<bool> continue_failed;
		uint8_t font_size;
		uint32_t text_color;
		uint32_t background_color;

		void layout();

		static void continueOfflineCallback(lv_event_t* event);
		static void continueOfflineJob(const void* context, bool* ok, char* message, size_t message_len);

public:
		explicit WifiSetupScreen(DeviceConfig* device_config);
		~WifiSetupScreen() override;
		// Reuses the boot screen font size and colors; call before create().
		void setAppearance(uint8_t font_size, uint32_t text_color, uint32_t background_color);
		void setLogo(const lv_image_dsc_t* source);
		void create() override;
		void destroy() override;
		void show() override;
		void hide() override;
		void update() override;
};

#endif

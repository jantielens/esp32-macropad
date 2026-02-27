#ifndef INFO_SCREEN_H
#define INFO_SCREEN_H

#include "screen.h"
#include "../config_manager.h"
#include <lvgl.h>

// Forward declaration
class DisplayManager;

// ============================================================================
// Info Screen
// ============================================================================
// Displays device information and real-time stats.
// Designed for round 240x240 minimum - all text centered.

class InfoScreen : public Screen {
private:
		lv_obj_t* screen;
		DeviceConfig* config;
		DisplayManager* displayMgr;

		uint32_t lastUpdateMs;
		
		// Labels (updated in real-time)
		lv_obj_t* deviceNameLabel;
		lv_obj_t* mdnsLabel;

		lv_obj_t* ipLabel;
		lv_obj_t* versionLabel;
		lv_obj_t* uptimeLabel;
		lv_obj_t* heapLabel;
		lv_obj_t* chipLabel;
		
		// Visual separators
		lv_obj_t* separatorTop;
		lv_obj_t* separatorBottom;
		
		// Touch event handler (static callback)
		static void touchEventCallback(lv_event_t* e);
		
public:
		InfoScreen(DeviceConfig* deviceConfig, DisplayManager* manager);
		~InfoScreen();
		
		void create() override;
		void destroy() override;
		void show() override;
		void hide() override;
		void update() override;
};

#endif // INFO_SCREEN_H

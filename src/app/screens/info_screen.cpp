#include "info_screen.h"
#include "../../version.h"
#include "log_manager.h"
#include "../device_telemetry.h"
#include "../board_config.h"
#include "../display_manager.h"
#include <WiFi.h>
#include <esp_chip_info.h>

InfoScreen::InfoScreen(DeviceConfig* deviceConfig, DisplayManager* manager) 
		: screen(nullptr), config(deviceConfig), displayMgr(manager),
		lastUpdateMs(0),
		deviceNameLabel(nullptr), mdnsLabel(nullptr), ipLabel(nullptr),
		versionLabel(nullptr), uptimeLabel(nullptr), heapLabel(nullptr), chipLabel(nullptr) {}

InfoScreen::~InfoScreen() {
		destroy();
}

void InfoScreen::create() {
		if (screen) return;  // Already created
		
		// Create main screen container
		screen = lv_obj_create(NULL);
		// Override theme background to pure black
		lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
		
		// All text centered for round display compatibility
		// 7 items ordered with project name as hero (largest font) in center
		// Spacing: 20px between items
		
		// Uptime (shortest - top)
		uptimeLabel = lv_label_create(screen);
		lv_obj_set_style_text_color(uptimeLabel, lv_color_make(200, 200, 200), 0);
		lv_obj_align(uptimeLabel, LV_ALIGN_CENTER, 0, -60);
		lv_obj_clear_flag(uptimeLabel, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Firmware version
		versionLabel = lv_label_create(screen);
		lv_label_set_text(versionLabel, "v" FIRMWARE_VERSION);
		lv_obj_set_style_text_color(versionLabel, lv_color_make(200, 200, 200), 0);
		lv_obj_align(versionLabel, LV_ALIGN_CENTER, 0, -40);
		lv_obj_clear_flag(versionLabel, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Free heap with CPU usage
		heapLabel = lv_label_create(screen);
		lv_obj_set_style_text_color(heapLabel, lv_color_make(200, 200, 200), 0);
		lv_obj_align(heapLabel, LV_ALIGN_CENTER, 0, -25);
		lv_obj_clear_flag(heapLabel, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Device name (HERO - center of screen, larger font)
		deviceNameLabel = lv_label_create(screen);
		lv_label_set_text(deviceNameLabel, "...");  // Will be set in update()
		lv_obj_set_style_text_color(deviceNameLabel, lv_color_white(), 0);
		lv_obj_set_style_text_font(deviceNameLabel, &lv_font_montserrat_18, 0);
		lv_obj_align(deviceNameLabel, LV_ALIGN_CENTER, 0, 0);
		lv_obj_clear_flag(deviceNameLabel, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Separator lines above and below device name (full width)
		separatorTop = lv_obj_create(screen);
		lv_obj_set_size(separatorTop, lv_pct(100), 1);  // Full width, 1px tall
		lv_obj_set_style_bg_color(separatorTop, lv_color_make(80, 80, 80), 0);
		lv_obj_set_style_border_width(separatorTop, 0, 0);
		lv_obj_set_style_pad_all(separatorTop, 0, 0);
		lv_obj_align(separatorTop, LV_ALIGN_CENTER, 0, -15);  // 15px above device name
		lv_obj_clear_flag(separatorTop, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		separatorBottom = lv_obj_create(screen);
		lv_obj_set_size(separatorBottom, lv_pct(100), 1);  // Full width, 1px tall
		lv_obj_set_style_bg_color(separatorBottom, lv_color_make(80, 80, 80), 0);
		lv_obj_set_style_border_width(separatorBottom, 0, 0);
		lv_obj_set_style_pad_all(separatorBottom, 0, 0);
		lv_obj_align(separatorBottom, LV_ALIGN_CENTER, 0, 15);  // 15px below device name
		lv_obj_clear_flag(separatorBottom, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Chip info (below center)
		chipLabel = lv_label_create(screen);
		char chip_text[64];
		snprintf(chip_text, sizeof(chip_text), "%s Rev %d", ESP.getChipModel(), ESP.getChipRevision());
		lv_label_set_text(chipLabel, chip_text);
		lv_obj_set_style_text_color(chipLabel, lv_color_make(150, 150, 150), 0);
		lv_obj_align(chipLabel, LV_ALIGN_CENTER, 0, 25);
		lv_obj_clear_flag(chipLabel, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// mDNS hostname
		mdnsLabel = lv_label_create(screen);
		lv_obj_set_style_text_color(mdnsLabel, lv_color_make(150, 150, 150), 0);
		lv_obj_align(mdnsLabel, LV_ALIGN_CENTER, 0, 40);
		lv_obj_clear_flag(mdnsLabel, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// IP Address (bottom)
		ipLabel = lv_label_create(screen);
		lv_obj_set_style_text_color(ipLabel, lv_color_make(100, 200, 255), 0);
		lv_obj_align(ipLabel, LV_ALIGN_CENTER, 0, 60);
		lv_obj_clear_flag(ipLabel, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		

		
		// Add touch event handler - tap anywhere to go to TestScreen
		lv_obj_add_event_cb(screen, touchEventCallback, LV_EVENT_CLICKED, this);
		lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
}

void InfoScreen::destroy() {
		if (screen) {
				lv_obj_delete(screen);
				screen = nullptr;
				deviceNameLabel = nullptr;
				mdnsLabel = nullptr;
				ipLabel = nullptr;
				versionLabel = nullptr;
				uptimeLabel = nullptr;
				heapLabel = nullptr;
				chipLabel = nullptr;
				separatorTop = nullptr;
				separatorBottom = nullptr;
		}
}

void InfoScreen::show() {
		if (screen) {
				lv_screen_load(screen);
		}
}

void InfoScreen::hide() {
		// Nothing to do - LVGL handles screen switching
}

void InfoScreen::update() {
		if (!screen) return;

		// This method is called from the LVGL task loop, potentially every 1-10ms.
		// Avoid constant label rewrites/invalidation: update at a modest cadence.
		const uint32_t now = millis();
		const uint32_t kUpdateIntervalMs = 500;
		if (lastUpdateMs != 0 && (uint32_t)(now - lastUpdateMs) < kUpdateIntervalMs) {
				return;
		}
		lastUpdateMs = now;
		
		// Update device name (from config)
		if (deviceNameLabel) {
				if (strlen(config->device_name) > 0) {
						lv_label_set_text(deviceNameLabel, config->device_name);
				} else {
						lv_label_set_text(deviceNameLabel, "ESP32 Device");
				}
		}
		
		// Update uptime (formatted)
		if (uptimeLabel) {
				unsigned long uptime_sec = millis() / 1000;
				char uptime_text[32];
				if (uptime_sec < 60) {
						snprintf(uptime_text, sizeof(uptime_text), "%lus", uptime_sec);
				} else if (uptime_sec < 3600) {
						snprintf(uptime_text, sizeof(uptime_text), "%lum %lus", uptime_sec / 60, uptime_sec % 60);
				} else {
						unsigned long hours = uptime_sec / 3600;
						unsigned long mins = (uptime_sec % 3600) / 60;
						snprintf(uptime_text, sizeof(uptime_text), "%luh %lum", hours, mins);
				}
				lv_label_set_text(uptimeLabel, uptime_text);
		}
		
		// Update free heap with CPU usage
		if (heapLabel) {
				char heap_text[64];
				unsigned long heap_kb = ESP.getFreeHeap() / 1024;
				const int cpu_usage = device_telemetry_get_cpu_usage();
				if (cpu_usage >= 0) {
						snprintf(heap_text, sizeof(heap_text), "%lu KB free / %d%% CPU", heap_kb, cpu_usage);
				} else {
						snprintf(heap_text, sizeof(heap_text), "%lu KB free / --%% CPU", heap_kb);
				}
				lv_label_set_text(heapLabel, heap_text);
		}
		
		// Update IP address
		if (ipLabel) {
				if (WiFi.status() == WL_CONNECTED) {
						lv_label_set_text(ipLabel, WiFi.localIP().toString().c_str());
				} else if (WiFi.getMode() == WIFI_AP) {
						lv_label_set_text(ipLabel, WiFi.softAPIP().toString().c_str());
				} else {
						lv_label_set_text(ipLabel, "No IP");
				}
		}
		
		// Update mDNS hostname
		if (mdnsLabel) {
				char sanitized[CONFIG_DEVICE_NAME_MAX_LEN];
				config_manager_sanitize_device_name(config->device_name, sanitized, sizeof(sanitized));
				char mdns_text[CONFIG_DEVICE_NAME_MAX_LEN + 10];
				snprintf(mdns_text, sizeof(mdns_text), "%s.local", sanitized);
				lv_label_set_text(mdnsLabel, mdns_text);
		}
}

// Touch event callback - navigate to TestScreen
void InfoScreen::touchEventCallback(lv_event_t* e) {
		InfoScreen* instance = (InfoScreen*)lv_event_get_user_data(e);
		if (instance && instance->displayMgr) {
				instance->displayMgr->showTest();
		}
}

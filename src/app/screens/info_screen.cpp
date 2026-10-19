#include "info_screen.h"

#include "../board_config.h"
#include "../display_manager.h"
#include "../display_geometry.h"
#include "../swipe_actions.h"
#include "../wifi_manager.h"
#include "splash_screen.h"
#include "system_screen_layout.h"

#include <WiFi.h>
#include <stdio.h>
#include <string.h>

InfoScreen::InfoScreen(DeviceConfig* deviceConfig, DisplayManager* manager)
		: screen(nullptr), config(deviceConfig), displayMgr(manager),
		  content(nullptr), logoContainer(nullptr), logoImage(nullptr),
		  deviceNameLabel(nullptr), networkLabel(nullptr), instructionsLabel(nullptr),
		  ipLabel(nullptr), mdnsLabel(nullptr), lastUpdateMs(0), refreshPending(true),
		  fontSize(BOOT_APPEARANCE_DEFAULT_FONT_SIZE),
		  textColor(BOOT_APPEARANCE_DEFAULT_TEXT_RGB),
		  backgroundColor(BOOT_APPEARANCE_DEFAULT_BACKGROUND_RGB) {}

InfoScreen::~InfoScreen() {
		destroy();
}

void InfoScreen::setAppearance(uint8_t font_size, uint32_t text_color, uint32_t background_color) {
		fontSize = font_size;
		textColor = text_color;
		backgroundColor = background_color;
}

void InfoScreen::create() {
		if (screen) return;
		screen = lv_obj_create(nullptr);
		lv_obj_set_style_bg_color(screen, lv_color_hex(backgroundColor), 0);
		lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(screen, 0, 0);
		lv_obj_set_style_pad_all(screen, 0, 0);
		lv_obj_set_scroll_dir(screen, LV_DIR_VER);
		lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_AUTO);

		const int screen_w = lv_obj_get_width(screen);
		const int screen_h = lv_obj_get_height(screen);
#if DISPLAY_SHAPE == DISPLAY_SHAPE_ROUND
		const int width = (screen_w < screen_h ? screen_w : screen_h) * 70 / 100;
#else
		const DisplayContentRect rect = display_safe_content_rect(screen_w, screen_h);
		const int width = rect.w - 32;
#endif
		const lv_font_t* font = splash_screen_font(fontSize, screen_w);
		content = lv_obj_create(screen);
		lv_obj_remove_style_all(content);
		lv_obj_set_width(content, width);
		lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE);
		deviceNameLabel = system_screen_label(content, width, font, textColor);
		networkLabel = system_screen_label(content, width, font, textColor);
		instructionsLabel = system_screen_label(content, width, font, textColor);
		ipLabel = system_screen_label(content, width, font, textColor);
		mdnsLabel = system_screen_label(content, width, font, textColor);

		lv_obj_add_event_cb(screen, touchEventCallback, LV_EVENT_CLICKED, this);
		lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
		swipe_actions_register(screen);
}

void InfoScreen::setLogo(const lv_image_dsc_t* source) {
		if (!screen) create();
		system_screen_set_logo(content, source, logoContainer, logoImage);
		refreshPending = true;
}

void InfoScreen::layout() {
		lv_obj_t* items[] = {deviceNameLabel, networkLabel, instructionsLabel, ipLabel, mdnsLabel};
		system_screen_layout(screen, content, logoContainer, logoImage,
				items, sizeof(items) / sizeof(items[0]));
}

void InfoScreen::destroy() {
		if (screen) lv_obj_delete(screen);
		screen = nullptr;
		content = nullptr;
		logoContainer = nullptr;
		logoImage = nullptr;
		deviceNameLabel = nullptr;
		networkLabel = nullptr;
		instructionsLabel = nullptr;
		ipLabel = nullptr;
		mdnsLabel = nullptr;
		refreshPending = true;
}

void InfoScreen::show() {
		if (!screen) create();
		refreshPending = true;
		lv_obj_scroll_to_y(screen, 0, LV_ANIM_OFF);
		lv_screen_load(screen);
}

void InfoScreen::hide() {
}

void InfoScreen::update() {
		if (!screen) return;
		const uint32_t now = millis();
		if (!refreshPending && static_cast<uint32_t>(now - lastUpdateMs) < 500) return;
		lastUpdateMs = now;
		bool changed = refreshPending;
		refreshPending = false;
		auto set_text = [&changed](lv_obj_t* label, const char* text) {
				if (strcmp(lv_label_get_text(label), text) != 0) {
						lv_label_set_text(label, text);
						changed = true;
				}
		};
		set_text(deviceNameLabel, config && config->device_name[0] ? config->device_name : "ESP32 Device");

		if (WiFi.status() == WL_CONNECTED) {
				char network[64];
				snprintf(network, sizeof(network), "Connected to: %s", WiFi.SSID().c_str());
				set_text(networkLabel, network);
				const IPAddress ip = WiFi.localIP();
				if (ip != IPAddress(0, 0, 0, 0)) {
						set_text(instructionsLabel, "To configure your device, open:");
						char url[CONFIG_DEVICE_NAME_MAX_LEN + 24];
						snprintf(url, sizeof(url), "http://%s/", ip.toString().c_str());
						set_text(ipLabel, url);
						char hostname[CONFIG_DEVICE_NAME_MAX_LEN];
						wifi_manager_get_mdns_hostname(hostname, sizeof(hostname));
						if (hostname[0]) {
								snprintf(url, sizeof(url), "or http://%s.local/", hostname);
								set_text(mdnsLabel, url);
						} else {
								set_text(mdnsLabel, "");
						}
				} else {
						set_text(instructionsLabel, "Waiting for an IP address...");
						set_text(ipLabel, "");
						set_text(mdnsLabel, "");
				}
		} else {
				set_text(networkLabel, "Not connected to Wi-Fi");
				set_text(instructionsLabel, WiFi.getMode() == WIFI_OFF
						? "Connect to Wi-Fi to configure your device."
						: "Waiting for a Wi-Fi connection...");
				set_text(ipLabel, "");
				set_text(mdnsLabel, "");
		}
		if (changed) layout();
}

void InfoScreen::touchEventCallback(lv_event_t* e) {
		auto* instance = static_cast<InfoScreen*>(lv_event_get_user_data(e));
		if (instance && instance->displayMgr) instance->displayMgr->goBack();
}

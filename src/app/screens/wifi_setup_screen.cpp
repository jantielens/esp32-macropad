#include "wifi_setup_screen.h"

#include "../board_config.h"
#include "../display_geometry.h"
#include "../log_manager.h"
#include "../main_loop_bridge.h"
#include "splash_screen.h"
#include "../web_portal_ap.h"

#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

bool app_continue_offline_from_setup();

namespace {

constexpr const char* kOfflineButtonText = "Stop AP and\ncontinue offline";

lv_obj_t* create_wrapped_label(lv_obj_t* parent, int width, const lv_font_t* font, uint32_t color) {
		lv_obj_t* label = lv_label_create(parent);
		lv_obj_set_width(label, width);
		lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
		lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_style_text_font(label, font, 0);
		lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
		lv_label_set_text(label, "");
		return label;
}

}

WifiSetupScreen::WifiSetupScreen(DeviceConfig* device_config)
		: config(device_config), screen(nullptr), content(nullptr), title_label(nullptr),
		  logo_container(nullptr), logo_image(nullptr), logo_descriptor{}, logo_pixels(nullptr),
		  ssid_label(nullptr), password_label(nullptr), url_label(nullptr), status_label(nullptr),
		  timeout_label(nullptr), continue_button(nullptr), button_label(nullptr),
		  instructions_label(nullptr), feedback_label(nullptr),
		  details_loaded(false), continue_pending(false), continue_failed(false),
		  font_size(BOOT_APPEARANCE_DEFAULT_FONT_SIZE),
		  text_color(BOOT_APPEARANCE_DEFAULT_TEXT_RGB),
		  background_color(BOOT_APPEARANCE_DEFAULT_BACKGROUND_RGB) {}

WifiSetupScreen::~WifiSetupScreen() {
		destroy();
}

void WifiSetupScreen::setAppearance(uint8_t size, uint32_t text, uint32_t background) {
		font_size = size;
		text_color = text;
		background_color = background;
}

void WifiSetupScreen::create() {
		if (screen) return;
		screen = lv_obj_create(nullptr);
		lv_obj_set_style_bg_color(screen, lv_color_hex(background_color), 0);
		lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(screen, 0, 0);
		lv_obj_set_style_pad_all(screen, 0, 0);
		lv_obj_set_scroll_dir(screen, LV_DIR_VER);
		lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_AUTO);

		const int screen_w = lv_obj_get_width(screen);
		const int screen_h = lv_obj_get_height(screen);
#if DISPLAY_SHAPE == DISPLAY_SHAPE_ROUND
		// Keep the centered block inside the circle's widest band.
		const int width = (screen_w < screen_h ? screen_w : screen_h) * 70 / 100;
#else
		const DisplayContentRect rect = display_safe_content_rect(screen_w, screen_h);
		const int width = rect.w - 32;
#endif
		const lv_font_t* font = splash_screen_font(font_size, screen_w);

		content = lv_obj_create(screen);
		lv_obj_remove_style_all(content);
		lv_obj_set_width(content, width);
		lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

		title_label = create_wrapped_label(content, width, font, text_color);
		lv_label_set_text(title_label, "WiFi Access Point (AP):");
		status_label = create_wrapped_label(content, width, font, text_color);
		ssid_label = create_wrapped_label(content, width, font, text_color);
		password_label = create_wrapped_label(content, width, font, text_color);
		url_label = create_wrapped_label(content, width, font, text_color);
		timeout_label = create_wrapped_label(content, width, font, text_color);
		instructions_label = create_wrapped_label(content, width, font, text_color);
		feedback_label = create_wrapped_label(content, width, font, text_color);

#if HAS_TOUCH
		// Inverted colors keep the button legible in light and dark themes,
		// including black-and-white e-paper panels.
		const int line_h = lv_font_get_line_height(font);
		continue_button = lv_obj_create(content);
		lv_obj_remove_style_all(continue_button);
		lv_obj_set_size(continue_button, width, line_h * 2 + 8);
		lv_obj_set_style_bg_color(continue_button, lv_color_hex(text_color), 0);
		lv_obj_set_style_bg_opa(continue_button, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(continue_button, line_h / 2, 0);
		lv_obj_set_style_pad_all(continue_button, 0, 0);
		lv_obj_set_style_border_width(continue_button, 2, 0);
		lv_obj_set_style_border_color(continue_button, lv_color_hex(text_color), 0);
		lv_obj_set_style_text_color(continue_button, lv_color_hex(background_color), 0);
		lv_obj_set_style_bg_color(continue_button, lv_color_hex(background_color), LV_STATE_PRESSED);
		lv_obj_set_style_text_color(continue_button, lv_color_hex(text_color), LV_STATE_PRESSED);
		lv_obj_set_style_border_color(continue_button, lv_color_hex(text_color), LV_STATE_PRESSED);
		lv_obj_add_flag(continue_button, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_clear_flag(continue_button, LV_OBJ_FLAG_SCROLLABLE);
		button_label = lv_label_create(continue_button);
		lv_label_set_text(button_label, kOfflineButtonText);
		lv_obj_set_width(button_label, width - 16);
		lv_label_set_long_mode(button_label, LV_LABEL_LONG_WRAP);
		lv_obj_set_style_text_align(button_label, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_style_text_font(button_label, font, 0);
		lv_obj_clear_flag(button_label, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_update_layout(button_label);
		lv_obj_set_height(continue_button, lv_obj_get_height(button_label) + line_h + 8);
		lv_obj_center(button_label);
		lv_obj_add_event_cb(continue_button, continueOfflineCallback, LV_EVENT_CLICKED, this);
#endif
		layout();
}

void WifiSetupScreen::destroy() {
		releaseLogo();
		if (screen) lv_obj_delete(screen);
		screen = nullptr;
		content = nullptr;
		title_label = nullptr;
		ssid_label = nullptr;
		password_label = nullptr;
		url_label = nullptr;
		status_label = nullptr;
		timeout_label = nullptr;
		continue_button = nullptr;
		button_label = nullptr;
		instructions_label = nullptr;
		feedback_label = nullptr;
}

void WifiSetupScreen::takeLogo(SplashScreen& splash) {
		if (!screen) create();
		releaseLogo();
		const lv_image_dsc_t* source = splash.takeLogo(&logo_descriptor, &logo_pixels);
		if (!source) return;
		logo_container = lv_obj_create(content);
		lv_obj_remove_style_all(logo_container);
		lv_obj_clear_flag(logo_container, LV_OBJ_FLAG_SCROLLABLE);
		logo_image = lv_image_create(logo_container);
		lv_image_set_src(logo_image, source);
		lv_image_set_inner_align(logo_image, LV_IMAGE_ALIGN_CONTAIN);
}

void WifiSetupScreen::releaseLogo() {
		if (logo_container) lv_obj_delete(logo_container);
		logo_container = nullptr;
		logo_image = nullptr;
		if (logo_pixels) heap_caps_free(logo_pixels);
		logo_pixels = nullptr;
		logo_descriptor = {};
}

void WifiSetupScreen::show() {
		if (!screen) create();
		details_loaded = false;
		lv_screen_load(screen);
}

void WifiSetupScreen::hide() {
		releaseLogo();
}

void WifiSetupScreen::layout() {
		const int gap = lv_font_get_line_height(lv_obj_get_style_text_font(title_label, LV_PART_MAIN)) / 2;
		lv_obj_t* items[] = {title_label, status_label, ssid_label, password_label, url_label,
				instructions_label, timeout_label, feedback_label, continue_button};
		int text_height = 0;
		for (lv_obj_t* item : items) {
				if (!item) continue;
				const bool empty = lv_obj_check_type(item, &lv_label_class) &&
						lv_label_get_text(item)[0] == '\0';
				if (empty) {
						lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
				} else {
						lv_obj_clear_flag(item, LV_OBJ_FLAG_HIDDEN);
						lv_obj_update_layout(item);
						if (text_height) text_height += gap;
						text_height += lv_obj_get_height(item);
				}
		}
		int y = 0;
		if (logo_container) {
				const int screen_h = lv_obj_get_height(screen);
				int height = screen_h - 16 - text_height - gap;
				if (height > screen_h / 6) height = screen_h / 6;
				if (height >= gap * 2) {
						const auto* source = static_cast<const lv_image_dsc_t*>(lv_image_get_src(logo_image));
						const int width_scale = lv_obj_get_width(content) * 256 / source->header.w;
						const int height_scale = height * 256 / source->header.h;
						int scale = width_scale < height_scale ? width_scale : height_scale;
						if (scale > 256) scale = 256;
						const int logo_width = source->header.w * scale / 256;
						const int logo_height = source->header.h * scale / 256;
						lv_obj_set_size(logo_container, logo_width, logo_height);
						// Transform scaling alone leaves the original object bounds in the layout.
						lv_obj_set_size(logo_image, logo_width, logo_height);
						lv_obj_update_layout(logo_container);
						lv_obj_clear_flag(logo_container, LV_OBJ_FLAG_HIDDEN);
						lv_obj_align(logo_container, LV_ALIGN_TOP_MID, 0, 0);
						lv_obj_center(logo_image);
						y = lv_obj_get_height(logo_container) + gap;
				} else {
						lv_obj_add_flag(logo_container, LV_OBJ_FLAG_HIDDEN);
				}
		}
		for (lv_obj_t* item : items) {
				if (!item || lv_obj_has_flag(item, LV_OBJ_FLAG_HIDDEN)) continue;
				lv_obj_align(item, LV_ALIGN_TOP_MID, 0, y);
				y += lv_obj_get_height(item) + gap;
		}
		if (y) y -= gap;
		lv_obj_set_height(content, y);
		// Center vertically when it fits; otherwise start at the top and scroll.
		const int margin = 8;
		if (y + margin * 2 <= lv_obj_get_height(screen)) {
				lv_obj_center(content);
		} else {
				lv_obj_align(content, LV_ALIGN_TOP_MID, 0, margin);
		}
}

void WifiSetupScreen::update() {
		if (!screen) return;
		if (continue_failed.exchange(false)) {
				continue_pending = false;
				lv_obj_clear_state(continue_button, LV_STATE_DISABLED);
				lv_label_set_text(button_label, kOfflineButtonText);
				lv_label_set_text(feedback_label, "Cannot start offline. Finish any update or save, then try again.");
				layout();
		}
		if (details_loaded) return;
		details_loaded = true;
		char ssid[33];
		char password[CONFIG_AP_PASSWORD_MAX_LEN];
		char ip[16];
		if (!web_portal_ap_get_setup_info(ssid, sizeof(ssid), password, sizeof(password), ip, sizeof(ip))) {
				lv_label_set_text(status_label, "Setup Wi-Fi could not start. Restart the device to try again.");
				lv_label_set_text(ssid_label, "");
				lv_label_set_text(password_label, "");
				lv_label_set_text(url_label, "");
				lv_label_set_text(instructions_label, "");
		} else {
				char text[160];
				lv_label_set_text(status_label, "1. Connect to this network:");
				lv_label_set_text(ssid_label, ssid);
				if (password[0]) {
						snprintf(text, sizeof(text), "Password: %s", password);
				} else {
						strlcpy(text, "No password needed", sizeof(text));
				}
				lv_label_set_text(password_label, text);
				snprintf(text, sizeof(text), "2. Browse to http://%s/", ip);
				lv_label_set_text(url_label, text);
				lv_label_set_text(instructions_label, "3. Configure your device");
		}

		if (config && config->portal_idle_timeout_seconds) {
				char notice[112];
				snprintf(notice, sizeof(notice),
						"Sleeps after %u s of portal inactivity.",
						static_cast<unsigned>(config->portal_idle_timeout_seconds));
				lv_label_set_text(timeout_label, notice);
		} else {
				lv_label_set_text(timeout_label, "");
		}
		layout();
}

void WifiSetupScreen::continueOfflineCallback(lv_event_t* event) {
		if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
		auto* self = static_cast<WifiSetupScreen*>(lv_event_get_user_data(event));
		if (self->continue_pending) return;
		self->continue_pending = true;
		lv_label_set_text(self->button_label, "Starting offline...");
		lv_obj_add_state(self->continue_button, LV_STATE_DISABLED);
		lv_label_set_text(self->feedback_label, "");
		const LoopBridgeResult result = loop_bridge_enqueue(continueOfflineJob, &self, sizeof(self));
		if (result != LOOP_BRIDGE_OK) {
				self->continue_pending = false;
				lv_obj_clear_state(self->continue_button, LV_STATE_DISABLED);
				lv_label_set_text(self->button_label, kOfflineButtonText);
				lv_label_set_text(self->feedback_label, result == LOOP_BRIDGE_BUSY
						? "Device busy. Tap the button to try again."
						: "Could not start offline. Restart the device and try again.");
				LOGW("WiFi", "Could not queue Continue offline request (%d)", static_cast<int>(result));
				self->layout();
		}
}

void WifiSetupScreen::continueOfflineJob(const void* context, bool* ok, char* message, size_t message_len) {
		auto* self = *static_cast<WifiSetupScreen* const*>(context);
		*ok = app_continue_offline_from_setup();
		strlcpy(message, *ok ? "Continuing offline" : "Cannot continue offline during an update or configuration write",
				message_len);
		if (!*ok) self->continue_failed.store(true);
}

#include "screens/splash_screen.h"

#include "board_config.h"
#include "log_manager.h"
#include "png_assets.h"

#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

static int getScaleTier(int screen_w) {
		if (screen_w >= 600) return 2;
		if (screen_w >= 420) return 1;
		return 0;
}

static void getSplashLogoBounds(int screen_w, int screen_h, uint16_t* width, uint16_t* height) {
		if (!width || !height || screen_w <= 0 || screen_h <= 0) return;
		int max_w = screen_w - 24;
		int max_h = screen_h / 2 - 24;
#if DISPLAY_SHAPE == DISPLAY_SHAPE_ROUND
		const int diameter = screen_w < screen_h ? screen_w : screen_h;
		max_w = diameter * 70 / 100;
		max_h = diameter * 38 / 100;
#endif
		if (max_w < 1) max_w = 1;
		if (max_h < 1) max_h = 1;
		*width = static_cast<uint16_t>(max_w);
		*height = static_cast<uint16_t>(max_h);
}

const lv_font_t* splash_screen_font(uint8_t size, int screen_w) {
		if (size == 0) {
				const int tier = getScaleTier(screen_w);
				size = tier == 2 ? 24 : tier == 1 ? 18 : 14;
		}
		switch (size) {
				case 12: return &lv_font_montserrat_12;
				case 14: return &lv_font_montserrat_14;
				case 18: return &lv_font_montserrat_18;
				case 24: return &lv_font_montserrat_24;
				case 32: return &lv_font_montserrat_32;
				case 36: return &lv_font_montserrat_36;
				default: return LV_FONT_DEFAULT;
		}
}

SplashScreen::SplashScreen()
		: screen(nullptr), logoImg(nullptr), statusLabel(nullptr), spinner(nullptr),
		  customLogoDescriptor{}, customLogoPixels(nullptr),
		  fontSize(BOOT_APPEARANCE_DEFAULT_FONT_SIZE),
		  textColor(BOOT_APPEARANCE_DEFAULT_TEXT_RGB),
		  backgroundColor(BOOT_APPEARANCE_DEFAULT_BACKGROUND_RGB),
		  customLogoSelected(false) {}

SplashScreen::~SplashScreen() {
		destroy();
}

void SplashScreen::setAppearance(uint8_t font_size, uint32_t text_color,
		uint32_t background_color, bool custom_logo_selected) {
		fontSize = font_size;
		textColor = text_color;
		backgroundColor = background_color;
		customLogoSelected = custom_logo_selected;
}

bool SplashScreen::getLogoBounds(uint16_t* width, uint16_t* height) const {
		if (!screen || !width || !height) return false;
		getSplashLogoBounds(lv_obj_get_width(screen), lv_obj_get_height(screen), width, height);
		return *width > 0 && *height > 0;
}

bool SplashScreen::setCustomLogo(uint16_t* pixels, uint16_t width, uint16_t height) {
		if (!pixels || !width || !height || !logoImg || !screen) return false;

		releaseCustomLogo();
		memset(&customLogoDescriptor, 0, sizeof(customLogoDescriptor));
		customLogoDescriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
		customLogoDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
		customLogoDescriptor.header.w = width;
		customLogoDescriptor.header.h = height;
		customLogoDescriptor.header.stride = width * sizeof(uint16_t);
		customLogoDescriptor.data_size = static_cast<uint32_t>(width) * height * sizeof(uint16_t);
		customLogoDescriptor.data = reinterpret_cast<const uint8_t*>(pixels);
		customLogoPixels = pixels;
		lv_image_set_src(logoImg, &customLogoDescriptor);
		lv_image_set_scale(logoImg, 256);
		lv_obj_align(logoImg, LV_ALIGN_CENTER, 0, -lv_obj_get_height(screen) / 4);
		return true;
}

void SplashScreen::releaseCustomLogo() {
		if (customLogoPixels) {
				if (logoImg) 				lv_image_set_src(logoImg, nullptr);
				heap_caps_free(customLogoPixels);
				customLogoPixels = nullptr;
				memset(&customLogoDescriptor, 0, sizeof(customLogoDescriptor));
		}
}

const lv_image_dsc_t* SplashScreen::getLogo() const {
		if (customLogoPixels) return &customLogoDescriptor;
		return customLogoSelected ? nullptr : &img_logo;
}

void SplashScreen::create() {
		LOGI("Splash", "Create start");
		if (screen) return;

		screen = lv_obj_create(nullptr);
		lv_obj_set_style_bg_color(screen, lv_color_hex(backgroundColor), 0);
		lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

		const int screen_w = lv_obj_get_width(screen);
		const int screen_h = lv_obj_get_height(screen);
		const int tier = getScaleTier(screen_w);
		uint16_t logo_max_w = 0;
		uint16_t logo_max_h = 0;
		getSplashLogoBounds(screen_w, screen_h, &logo_max_w, &logo_max_h);

		logoImg = lv_image_create(screen);
		if (!customLogoSelected) {
				lv_image_set_src(logoImg, &img_logo);
				const int source_w = img_logo.header.w;
				const int source_h = img_logo.header.h;
				if (source_w > 0 && source_h > 0) {
						const float scale = fminf(
								1.0f,
								fminf(static_cast<float>(logo_max_w) / source_w,
									  static_cast<float>(logo_max_h) / source_h));
						lv_image_set_scale(logoImg, static_cast<uint16_t>(scale * 256.0f));
				}
		} else {
				lv_image_set_src(logoImg, nullptr);
		}
		lv_obj_align(logoImg, LV_ALIGN_CENTER, 0, -screen_h / 4);

		statusLabel = lv_label_create(screen);
		lv_label_set_text(statusLabel, "Starting...");
		lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_DOT);
#if DISPLAY_SHAPE == DISPLAY_SHAPE_ROUND
		const int label_width = (screen_w < screen_h ? screen_w : screen_h) * 68 / 100;
#else
		const int label_width = screen_w - 32;
#endif
		lv_obj_set_width(statusLabel, label_width > 0 ? label_width : 1);
		lv_obj_set_height(statusLabel, screen_h / 5);
		lv_obj_set_style_text_font(statusLabel, splash_screen_font(fontSize, screen_w), 0);
		lv_obj_set_style_text_color(statusLabel, lv_color_hex(textColor), 0);
		lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_align(statusLabel, LV_ALIGN_TOP_MID, 0, screen_h * 57 / 100);

		const int spinner_size = tier == 2 ? 60 : tier == 1 ? 50 : 40;
		const int arc_width = tier == 2 ? 6 : tier == 1 ? 5 : 4;
		spinner = lv_spinner_create(screen);
		lv_spinner_set_anim_params(spinner, 1000, 270);
		lv_obj_set_size(spinner, spinner_size, spinner_size);
		lv_obj_set_style_arc_color(spinner, lv_color_hex(textColor), LV_PART_INDICATOR);
		lv_obj_set_style_arc_color(spinner,
				lv_color_mix(lv_color_hex(textColor), lv_color_hex(backgroundColor), 48), LV_PART_MAIN);
		lv_obj_set_style_arc_width(spinner, arc_width, LV_PART_INDICATOR);
		lv_obj_set_style_arc_width(spinner, arc_width, LV_PART_MAIN);
		lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, screen_h * 88 / 100 - spinner_size / 2);

		LOGI("Splash", "Screen created");
}

void SplashScreen::destroy() {
		releaseCustomLogo();
		if (screen) {
				lv_obj_delete(screen);
				screen = nullptr;
				logoImg = nullptr;
				statusLabel = nullptr;
				spinner = nullptr;
		}
}

void SplashScreen::show() {
		if (screen) lv_screen_load(screen);
}

void SplashScreen::hide() {
}

void SplashScreen::update() {
}

void SplashScreen::setStatus(const char* text) {
		if (statusLabel) {
				LOGI("Splash", "Status: %s", text ? text : "(null)");
				lv_label_set_text(statusLabel, text ? text : "");
		} else {
				LOGE("Splash", "statusLabel is NULL");
		}
}

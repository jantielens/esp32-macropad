
#include "screens/splash_screen.h"
#include "log_manager.h"
#include "png_assets.h"

static void layoutSplashBlock(lv_obj_t* screen, lv_obj_t* logoImg, lv_obj_t* statusLabel, lv_obj_t* spinner) {
		if (!screen || !logoImg || !statusLabel || !spinner) return;

		// Ensure LVGL has calculated object sizes before we query heights
		lv_obj_update_layout(screen);
		lv_obj_update_layout(logoImg);
		lv_obj_update_layout(statusLabel);
		lv_obj_update_layout(spinner);

		const int top_margin = 12;
		const int gap_logo_to_status = 14;
		const int gap_status_to_spinner = 16;

		const int screen_h = (int)lv_obj_get_height(screen);
		const int logo_h = (int)lv_obj_get_height(logoImg);
		const int label_h = (int)lv_obj_get_height(statusLabel);
		const int spinner_h = (int)lv_obj_get_height(spinner);

		const int block_h = logo_h + gap_logo_to_status + label_h + gap_status_to_spinner + spinner_h;

		int top = (screen_h - block_h) / 2;
		if (block_h + top_margin * 2 > screen_h) {
				top = top_margin;
		}
		if (top < 0) top = 0;

		lv_obj_align(logoImg, LV_ALIGN_TOP_MID, 0, top);
		lv_obj_align_to(statusLabel, logoImg, LV_ALIGN_OUT_BOTTOM_MID, 0, gap_logo_to_status);
		lv_obj_align_to(spinner, statusLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, gap_status_to_spinner);
}

SplashScreen::SplashScreen() : screen(nullptr), logoImg(nullptr), statusLabel(nullptr), spinner(nullptr) {}

SplashScreen::~SplashScreen() {
		destroy();
}

void SplashScreen::create() {
		LOGI("Splash", "Create start");
		if (screen) {
				LOGI("Splash", "Already created");
				return;  // Already created
		}
		
		// Create screen
		screen = lv_obj_create(NULL);
		// Override theme background to pure black
		lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

		// Logo image
	logoImg = lv_image_create(screen);
	lv_image_set_src(logoImg, &img_logo);
		// Status text
		statusLabel = lv_label_create(screen);
		lv_label_set_text(statusLabel, "Booting...");
		lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
		lv_obj_set_width(statusLabel, (lv_coord_t)(lv_obj_get_width(screen) - 24));
		lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_style_text_color(statusLabel, lv_color_make(100, 100, 100), 0);

		// Spinner to show activity
	spinner = lv_spinner_create(screen);
	lv_spinner_set_anim_params(spinner, 1000, 60);
		lv_obj_set_size(spinner, 40, 40);
		lv_obj_set_style_arc_color(spinner, lv_color_make(0, 150, 255), LV_PART_INDICATOR);
		lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
		lv_obj_set_style_arc_color(spinner, lv_color_make(40, 40, 40), LV_PART_MAIN);
		lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);

		// Position the whole block.
		layoutSplashBlock(screen, logoImg, statusLabel, spinner);
		
		LOGI("Splash", "Screen created");
}

void SplashScreen::destroy() {
		if (screen) {
				lv_obj_delete(screen);
				screen = nullptr;
				logoImg = nullptr;
				statusLabel = nullptr;
				spinner = nullptr;
		}
}

void SplashScreen::show() {
		if (screen) {
				lv_screen_load(screen);
		}
}

void SplashScreen::hide() {
		// Nothing to do - LVGL handles screen switching
}

void SplashScreen::update() {
		// Static screen - no updates needed
}

void SplashScreen::setStatus(const char* text) {
		if (statusLabel) {
				LOGI("Splash", "Status: %s", text ? text : "(null)");
				lv_label_set_text(statusLabel, text);

				// Re-layout in case the text height changed (wrapping, font changes, etc.)
				layoutSplashBlock(screen, logoImg, statusLabel, spinner);
		} else {
				LOGE("Splash", "statusLabel is NULL");
		}
}

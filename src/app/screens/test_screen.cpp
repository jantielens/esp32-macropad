#include "test_screen.h"
#include "log_manager.h"
#include "../board_config.h"
#include "../display_manager.h"

TestScreen::TestScreen(DisplayManager* manager) 
		: screen(nullptr), displayMgr(manager),
			titleLabel(nullptr), redBar(nullptr), greenBar(nullptr), blueBar(nullptr),
			gradientBar(nullptr), yellowBar(nullptr), cyanBar(nullptr), magentaBar(nullptr),
			infoLabel(nullptr) {}

TestScreen::~TestScreen() {
		destroy();
}

void TestScreen::create() {
		if (screen) return;  // Already created

		LOGI("TestScreen", "Create start");
		
		// Create main screen container
		screen = lv_obj_create(NULL);
		// Override theme background to pure black
		lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
		
		// Title
		titleLabel = lv_label_create(screen);
		lv_label_set_text(titleLabel, "Display Test");
		lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);
		lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, 0);
		lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 8);  // Use TOP alignment to verify Y orientation
		lv_obj_clear_flag(titleLabel, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Red bar
		redBar = lv_obj_create(screen);
		lv_obj_set_size(redBar, lv_pct(100), 12);
		lv_obj_set_style_bg_color(redBar, lv_color_hex(0xFF0000), 0);
		lv_obj_set_style_border_width(redBar, 0, 0);
		lv_obj_set_style_pad_all(redBar, 0, 0);
		lv_obj_align(redBar, LV_ALIGN_CENTER, 0, -60);
		lv_obj_clear_flag(redBar, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Green bar
		greenBar = lv_obj_create(screen);
		lv_obj_set_size(greenBar, lv_pct(100), 12);
		lv_obj_set_style_bg_color(greenBar, lv_color_hex(0x00FF00), 0);
		lv_obj_set_style_border_width(greenBar, 0, 0);
		lv_obj_set_style_pad_all(greenBar, 0, 0);
		lv_obj_align(greenBar, LV_ALIGN_CENTER, 0, -45);
		lv_obj_clear_flag(greenBar, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Blue bar
		blueBar = lv_obj_create(screen);
		lv_obj_set_size(blueBar, lv_pct(100), 12);
		lv_obj_set_style_bg_color(blueBar, lv_color_hex(0x0000FF), 0);
		lv_obj_set_style_border_width(blueBar, 0, 0);
		lv_obj_set_style_pad_all(blueBar, 0, 0);
		lv_obj_align(blueBar, LV_ALIGN_CENTER, 0, -30);
		lv_obj_clear_flag(blueBar, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Grayscale gradient bar (centered for maximum width on round displays)
		gradientBar = lv_obj_create(screen);
		lv_obj_set_size(gradientBar, lv_pct(100), 40);
		lv_obj_set_style_bg_color(gradientBar, lv_color_black(), 0);
		lv_obj_set_style_bg_grad_color(gradientBar, lv_color_white(), 0);
		lv_obj_set_style_bg_grad_dir(gradientBar, LV_GRAD_DIR_HOR, 0);  // Horizontal gradient
		lv_obj_set_style_border_width(gradientBar, 0, 0);
		lv_obj_set_style_pad_all(gradientBar, 0, 0);
		lv_obj_align(gradientBar, LV_ALIGN_CENTER, 0, 0);
		lv_obj_clear_flag(gradientBar, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Yellow bar (R+G)
		yellowBar = lv_obj_create(screen);
		lv_obj_set_size(yellowBar, lv_pct(100), 12);
		lv_obj_set_style_bg_color(yellowBar, lv_color_hex(0xFFFF00), 0);
		lv_obj_set_style_border_width(yellowBar, 0, 0);
		lv_obj_set_style_pad_all(yellowBar, 0, 0);
		lv_obj_align(yellowBar, LV_ALIGN_CENTER, 0, 30);
		lv_obj_clear_flag(yellowBar, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Cyan bar (G+B)
		cyanBar = lv_obj_create(screen);
		lv_obj_set_size(cyanBar, lv_pct(100), 12);
		lv_obj_set_style_bg_color(cyanBar, lv_color_hex(0x00FFFF), 0);
		lv_obj_set_style_border_width(cyanBar, 0, 0);
		lv_obj_set_style_pad_all(cyanBar, 0, 0);
		lv_obj_align(cyanBar, LV_ALIGN_CENTER, 0, 45);
		lv_obj_clear_flag(cyanBar, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Magenta bar (R+B)
		magentaBar = lv_obj_create(screen);
		lv_obj_set_size(magentaBar, lv_pct(100), 12);
		lv_obj_set_style_bg_color(magentaBar, lv_color_hex(0xFF00FF), 0);
		lv_obj_set_style_border_width(magentaBar, 0, 0);
		lv_obj_set_style_pad_all(magentaBar, 0, 0);
		lv_obj_align(magentaBar, LV_ALIGN_CENTER, 0, 60);
		lv_obj_clear_flag(magentaBar, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Resolution info
		infoLabel = lv_label_create(screen);
		char info_text[32];
		snprintf(info_text, sizeof(info_text), "%dx%d RGB565", DISPLAY_WIDTH, DISPLAY_HEIGHT);
		lv_label_set_text(infoLabel, info_text);
		lv_obj_set_style_text_color(infoLabel, lv_color_make(150, 150, 150), 0);
		lv_obj_align(infoLabel, LV_ALIGN_BOTTOM_MID, 0, -8);  // Use BOTTOM alignment to verify Y orientation
		lv_obj_clear_flag(infoLabel, LV_OBJ_FLAG_CLICKABLE);  // Click-transparent
		
		// Add touch event handler - tap anywhere to go to InfoScreen
		lv_obj_add_event_cb(screen, touchEventCallback, LV_EVENT_CLICKED, this);
		lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
		
		LOGI("TestScreen", "Create complete");
}

void TestScreen::destroy() {
		if (screen) {
				lv_obj_delete(screen);
				screen = nullptr;
				titleLabel = nullptr;
				redBar = nullptr;
				greenBar = nullptr;
				blueBar = nullptr;
				gradientBar = nullptr;
				yellowBar = nullptr;
				cyanBar = nullptr;
				magentaBar = nullptr;
				infoLabel = nullptr;
		}
}

void TestScreen::show() {
		if (screen) {
				lv_screen_load(screen);
		}
}

void TestScreen::hide() {
		// Nothing to do - LVGL handles screen switching
}

void TestScreen::update() {
		// Static screen - no dynamic updates needed
}

// Touch event callback - navigate to InfoScreen
void TestScreen::touchEventCallback(lv_event_t* e) {
		TestScreen* instance = (TestScreen*)lv_event_get_user_data(e);
		if (instance && instance->displayMgr) {
				instance->displayMgr->showInfo();
		}
}

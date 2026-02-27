#include "fps_screen.h"
#include "log_manager.h"
#include "../board_config.h"
#include "../display_manager.h"

FpsScreen::FpsScreen(DisplayManager* manager)
		: screen(nullptr), displayMgr(manager),
			fpsValueLabel(nullptr), fpsUnitLabel(nullptr),
			presentLabel(nullptr), renderLabel(nullptr), frameLabel(nullptr),
			arc(nullptr), arcAngle(0) {}

FpsScreen::~FpsScreen() {
		destroy();
}

void FpsScreen::create() {
		if (screen) return;

		LOGI("FpsScreen", "Create start");

		screen = lv_obj_create(NULL);
		lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

		// --- Spinning arc (centered, behind text) ---
		arc = lv_arc_create(screen);
		lv_obj_set_size(arc, 120, 120);
		lv_obj_align(arc, LV_ALIGN_CENTER, 0, -20);
		lv_arc_set_rotation(arc, 0);
		lv_arc_set_bg_angles(arc, 0, 360);
		lv_arc_set_angles(arc, 0, 90);
		lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
		lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
		// Background track
		lv_obj_set_style_arc_color(arc, lv_color_hex(0x222222), LV_PART_MAIN);
		lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
		// Indicator (spinning part)
		lv_obj_set_style_arc_color(arc, lv_color_hex(0x3399FF), LV_PART_INDICATOR);
		lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);

		// --- Large FPS value (centered inside arc) ---
		fpsValueLabel = lv_label_create(screen);
		lv_label_set_text(fpsValueLabel, "--");
		lv_obj_set_style_text_color(fpsValueLabel, lv_color_white(), 0);
		lv_obj_set_style_text_font(fpsValueLabel, &lv_font_montserrat_24, 0);
		lv_obj_align(fpsValueLabel, LV_ALIGN_CENTER, 0, -28);
		lv_obj_clear_flag(fpsValueLabel, LV_OBJ_FLAG_CLICKABLE);

		fpsUnitLabel = lv_label_create(screen);
		lv_label_set_text(fpsUnitLabel, "FPS");
		lv_obj_set_style_text_color(fpsUnitLabel, lv_color_hex(0x3399FF), 0);
		lv_obj_set_style_text_font(fpsUnitLabel, &lv_font_montserrat_14, 0);
		lv_obj_align(fpsUnitLabel, LV_ALIGN_CENTER, 0, -4);
		lv_obj_clear_flag(fpsUnitLabel, LV_OBJ_FLAG_CLICKABLE);

		// --- Stats labels (below arc) ---
		presentLabel = lv_label_create(screen);
		lv_label_set_text(presentLabel, "Present:  -- ms");
		lv_obj_set_style_text_color(presentLabel, lv_color_hex(0xBBBBBB), 0);
		lv_obj_set_style_text_font(presentLabel, &lv_font_montserrat_14, 0);
		lv_obj_align(presentLabel, LV_ALIGN_CENTER, 0, 55);
		lv_obj_clear_flag(presentLabel, LV_OBJ_FLAG_CLICKABLE);

		renderLabel = lv_label_create(screen);
		lv_label_set_text(renderLabel, "Render:   -- ms");
		lv_obj_set_style_text_color(renderLabel, lv_color_hex(0xBBBBBB), 0);
		lv_obj_set_style_text_font(renderLabel, &lv_font_montserrat_14, 0);
		lv_obj_align(renderLabel, LV_ALIGN_CENTER, 0, 73);
		lv_obj_clear_flag(renderLabel, LV_OBJ_FLAG_CLICKABLE);

		frameLabel = lv_label_create(screen);
		lv_label_set_text(frameLabel, "Frame:    -- ms");
		lv_obj_set_style_text_color(frameLabel, lv_color_hex(0xBBBBBB), 0);
		lv_obj_set_style_text_font(frameLabel, &lv_font_montserrat_14, 0);
		lv_obj_align(frameLabel, LV_ALIGN_CENTER, 0, 91);
		lv_obj_clear_flag(frameLabel, LV_OBJ_FLAG_CLICKABLE);

		LOGI("FpsScreen", "Create complete");
}

void FpsScreen::destroy() {
		if (screen) {
				lv_obj_delete(screen);
				screen = nullptr;
				fpsValueLabel = nullptr;
				fpsUnitLabel = nullptr;
				presentLabel = nullptr;
				renderLabel = nullptr;
				frameLabel = nullptr;
				arc = nullptr;
		}
}

void FpsScreen::show() {
		if (screen) {
				arcAngle = 0;
				lv_screen_load(screen);
		}
}

void FpsScreen::hide() {
		// Nothing to do - LVGL handles screen switching
}

void FpsScreen::update() {
		if (!screen) return;

		// Advance the spinning arc every frame.
		// The arc step is large enough to be visible even at low FPS.
		arcAngle = (arcAngle + 15) % 360;
		lv_arc_set_angles(arc, arcAngle, arcAngle + 90);

		// Force LVGL to redraw the entire screen every frame.
		// This ensures we measure the maximum achievable panel refresh rate
		// rather than only counting frames with organic UI changes.
		lv_obj_invalidate(lv_screen_active());

		// Read perf stats (updated every ~1s by the render/present task).
		DisplayPerfStats stats;
		if (display_manager_get_perf_stats(&stats)) {
				char buf[32];

				snprintf(buf, sizeof(buf), "%u", stats.fps);
				lv_label_set_text(fpsValueLabel, buf);

				uint32_t present_ms = (stats.present_us + 500) / 1000;
				uint32_t render_ms  = (stats.lv_timer_us + 500) / 1000;
				uint32_t frame_ms   = present_ms + render_ms;

				snprintf(buf, sizeof(buf), "Present:  %lu ms", (unsigned long)present_ms);
				lv_label_set_text(presentLabel, buf);

				snprintf(buf, sizeof(buf), "Render:   %lu ms", (unsigned long)render_ms);
				lv_label_set_text(renderLabel, buf);

				snprintf(buf, sizeof(buf), "Frame:    %lu ms", (unsigned long)frame_ms);
				lv_label_set_text(frameLabel, buf);
		}
}

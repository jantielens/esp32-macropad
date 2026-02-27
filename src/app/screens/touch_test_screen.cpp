#include "touch_test_screen.h"

#if HAS_TOUCH

#include "../board_config.h"
#include "../log_manager.h"
#include "../touch_manager.h"

#include <stdlib.h>  // abs()

TouchTestScreen::TouchTestScreen()
		: screen(nullptr), canvas(nullptr), canvasBuf(nullptr),
			headerLabel(nullptr),
				prevTouchValid(false), prevX(0), prevY(0), brushRadius(3),
			canvasWidth(0), canvasHeight(0) {}

TouchTestScreen::~TouchTestScreen() {
		destroy();
}

void TouchTestScreen::create() {
		if (screen) return;

		LOGI("TouchTest", "Create start");
		
		// Query logical (post-rotation) dimensions from LVGL.
		canvasWidth  = (uint16_t)lv_display_get_horizontal_resolution(NULL);
		canvasHeight = (uint16_t)lv_display_get_vertical_resolution(NULL);
		
		// Adaptive brush size: ~0.8% of the smaller display dimension, clamped 2-6px
		uint16_t minDim = canvasWidth < canvasHeight ? canvasWidth : canvasHeight;
		brushRadius = (uint8_t)(minDim * 8 / 1000);
		if (brushRadius < 2) brushRadius = 2;
		if (brushRadius > 6) brushRadius = 6;

		// Create the LVGL screen object (always lightweight)
		screen = lv_obj_create(NULL);
		lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

		// Header label — small, semi-transparent, top-center
		headerLabel = lv_label_create(screen);
		char header[48];
		snprintf(header, sizeof(header), "Touch Test  %dx%d", canvasWidth, canvasHeight);
		lv_label_set_text(headerLabel, header);
		lv_obj_set_style_text_color(headerLabel, lv_color_make(80, 80, 80), 0);
		lv_obj_set_style_text_font(headerLabel, &lv_font_montserrat_14, 0);
		lv_obj_align(headerLabel, LV_ALIGN_TOP_MID, 0, 4);
		lv_obj_clear_flag(headerLabel, LV_OBJ_FLAG_CLICKABLE);

		// Canvas is NOT allocated here — deferred to show() to save PSRAM.

		LOGI("TouchTest", "Create complete (brush r=%d)", brushRadius);
}

void TouchTestScreen::destroy() {
		if (canvas) {
				lv_obj_delete(canvas);
				canvas = nullptr;
		}
		if (canvasBuf) {
				heap_caps_free(canvasBuf);
				canvasBuf = nullptr;
		}
		if (screen) {
				lv_obj_delete(screen);
				screen = nullptr;
				headerLabel = nullptr;
		}
}

void TouchTestScreen::show() {
		if (!screen) return;

		// Allocate canvas buffer in PSRAM (only while this screen is active)
		if (!canvasBuf) {
				size_t bufSize = LV_CANVAS_BUF_SIZE(canvasWidth, canvasHeight, 16, 1);
				// Try PSRAM first, fall back to internal RAM (e.g. CYD v2 has no PSRAM)
				canvasBuf = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
				if (!canvasBuf) {
						canvasBuf = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_DEFAULT);
				}
				if (!canvasBuf) {
						LOGE("TouchTest", "Failed to allocate canvas (%u bytes)", (unsigned)bufSize);
						// Show error on screen so the user knows why touch test is unavailable
						lv_label_set_text(headerLabel, "");
						lv_obj_align(headerLabel, LV_ALIGN_CENTER, 0, 0);
						lv_label_set_text_fmt(headerLabel,
								"Touch Test Unavailable\n\n"
								"Not enough memory to allocate\n"
								"canvas buffer (%u KB required).\n\n"
								"This board may lack PSRAM.",
								(unsigned)(bufSize / 1024));
						lv_obj_set_style_text_color(headerLabel, lv_color_make(200, 80, 80), 0);
						lv_obj_set_style_text_align(headerLabel, LV_TEXT_ALIGN_CENTER, 0);
						lv_screen_load(screen);
						return;
				}
				LOGI("TouchTest", "Canvas buffer allocated: %u KB %s",
						 (unsigned)(bufSize / 1024),
						 heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 0 ? "PSRAM" : "internal");
		}

		// Create or re-create the canvas widget
		if (canvas) {
				lv_obj_delete(canvas);
				canvas = nullptr;
		}

		canvas = lv_canvas_create(screen);
		lv_canvas_set_buffer(canvas, canvasBuf, canvasWidth, canvasHeight, LV_COLOR_FORMAT_RGB565);
		lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);

		// Clear canvas to black
		lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);

		// Make canvas receive touch events
		lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(canvas, touchEventCallback, LV_EVENT_PRESSING, this);
		lv_obj_add_event_cb(canvas, touchEventCallback, LV_EVENT_RELEASED, this);

		// Move header label to front (above canvas)
		lv_obj_move_to_index(headerLabel, -1);

		// Reset touch tracking
		prevTouchValid = false;

		// Suppress touch input briefly to discard any residual press
		// from the previous screen (ghost dot prevention).
		touch_manager_suppress_lvgl_input(200);

		lv_screen_load(screen);

		// Log canvas geometry for diagnostics
		lv_area_t ca;
		lv_obj_get_coords(canvas, &ca);
		LOGI("TouchTest", "Screen shown  canvas=%dx%d  pos=(%d,%d)-(%d,%d)",
				 canvasWidth, canvasHeight, ca.x1, ca.y1, ca.x2, ca.y2);
}

void TouchTestScreen::hide() {
		prevTouchValid = false;

		// Free canvas and PSRAM buffer when leaving (zero cost while inactive)
		if (canvas) {
				lv_obj_delete(canvas);
				canvas = nullptr;
		}
		if (canvasBuf) {
				heap_caps_free(canvasBuf);
				canvasBuf = nullptr;
				LOGI("TouchTest", "Canvas buffer freed");
		}
}

void TouchTestScreen::update() {
		// No periodic updates needed — drawing happens in touch event callback
}

// ============================================================================
// Drawing helpers
// ============================================================================

void TouchTestScreen::drawDot(int16_t cx, int16_t cy, lv_color_t color, uint8_t radius) {
		if (!canvas) return;

		lv_draw_rect_dsc_t rect_dsc;
		lv_draw_rect_dsc_init(&rect_dsc);
		rect_dsc.bg_color = color;
		rect_dsc.bg_opa = LV_OPA_COVER;
		rect_dsc.radius = radius;
		rect_dsc.border_width = 0;

		int16_t x1 = cx - radius;
		int16_t y1 = cy - radius;
		int16_t x2 = cx + radius;
		int16_t y2 = cy + radius;

		// Clamp to canvas bounds
		if (x1 < 0) x1 = 0;
		if (y1 < 0) y1 = 0;
		if (x2 >= canvasWidth) x2 = canvasWidth - 1;
		if (y2 >= canvasHeight) y2 = canvasHeight - 1;

		lv_layer_t layer;
		lv_canvas_init_layer(canvas, &layer);
		lv_area_t coords = {x1, y1, x2, y2};
		lv_draw_rect(&layer, &rect_dsc, &coords);
		lv_canvas_finish_layer(canvas, &layer);
}

void TouchTestScreen::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
		// Interpolate white dots between two touch points so fast movements don't leave gaps.
		// Uses a thinner brush so the red touch-point dots remain visible on top.
		if (!canvas) return;
		uint8_t lineRadius = (brushRadius > 1) ? brushRadius - 1 : 1;

		int16_t dx = x1 - x0;
		int16_t dy = y1 - y0;
		int16_t dist = (int16_t)sqrt((float)(dx * dx + dy * dy));

		int16_t step = lineRadius;  // step = radius for nice overlap
		if (step < 1) step = 1;

		if (dist > step) {
				// Batch all interpolated dots into a single layer session for performance
				lv_layer_t layer;
				lv_canvas_init_layer(canvas, &layer);

				lv_draw_rect_dsc_t rect_dsc;
				lv_draw_rect_dsc_init(&rect_dsc);
				rect_dsc.bg_color = lv_color_white();
				rect_dsc.bg_opa = LV_OPA_COVER;
				rect_dsc.radius = lineRadius;
				rect_dsc.border_width = 0;

				int16_t steps = dist / step;
				for (int16_t i = 1; i < steps; i++) {  // skip endpoints (dots drawn separately)
						int16_t x = x0 + (int16_t)((int32_t)dx * i / steps);
						int16_t y = y0 + (int16_t)((int32_t)dy * i / steps);

						int16_t lx1 = x - lineRadius;
						int16_t ly1 = y - lineRadius;
						int16_t lx2 = x + lineRadius;
						int16_t ly2 = y + lineRadius;
						if (lx1 < 0) lx1 = 0;
						if (ly1 < 0) ly1 = 0;
						if (lx2 >= canvasWidth) lx2 = canvasWidth - 1;
						if (ly2 >= canvasHeight) ly2 = canvasHeight - 1;

						lv_area_t coords = {lx1, ly1, lx2, ly2};
						lv_draw_rect(&layer, &rect_dsc, &coords);
				}

				lv_canvas_finish_layer(canvas, &layer);
		}
}

// ============================================================================
// Touch event callback
// ============================================================================

void TouchTestScreen::touchEventCallback(lv_event_t* e) {
		TouchTestScreen* self = (TouchTestScreen*)lv_event_get_user_data(e);
		if (!self || !self->canvas) return;

		lv_event_code_t code = lv_event_get_code(e);

		if (code == LV_EVENT_RELEASED) {
				self->prevTouchValid = false;
				return;
		}

		// LV_EVENT_PRESSING — finger is down and moving
		lv_indev_t* indev = lv_indev_active();
		if (!indev) return;

		lv_point_t point;
		lv_indev_get_point(indev, &point);

		// Convert screen-space coordinates to canvas-local coordinates.
		lv_area_t ca;
		lv_obj_get_coords(self->canvas, &ca);
		int16_t x = point.x - ca.x1;
		int16_t y = point.y - ca.y1;

		if (self->prevTouchValid) {
				// 1. White connecting line (thinner, drawn first)
				self->drawLine(self->prevX, self->prevY, x, y);
				// 2. Re-draw previous red dot on top (line may have partially covered it)
				self->drawDot(self->prevX, self->prevY, lv_color_hex(0xFF0000), self->brushRadius);
		}

		// 3. Red dot at current touch point (always on top)
		self->drawDot(x, y, lv_color_hex(0xFF0000), self->brushRadius);

		self->prevX = x;
		self->prevY = y;
		self->prevTouchValid = true;
}

#endif // HAS_TOUCH

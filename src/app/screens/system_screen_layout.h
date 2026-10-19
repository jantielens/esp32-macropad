#ifndef SYSTEM_SCREEN_LAYOUT_H
#define SYSTEM_SCREEN_LAYOUT_H

#include <lvgl.h>

inline lv_obj_t* system_screen_label(lv_obj_t* parent, int width,
		const lv_font_t* font, uint32_t color) {
		lv_obj_t* label = lv_label_create(parent);
		lv_obj_set_width(label, width);
		lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
		lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_style_text_font(label, font, 0);
		lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
		lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
		lv_label_set_text(label, "");
		return label;
}

inline void system_screen_set_logo(lv_obj_t* content, const lv_image_dsc_t* source,
		lv_obj_t*& container, lv_obj_t*& image) {
		if (container) lv_obj_delete(container);
		container = nullptr;
		image = nullptr;
		if (!source) return;
		container = lv_obj_create(content);
		lv_obj_remove_style_all(container);
		lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
		image = lv_image_create(container);
		lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
		lv_image_set_src(image, source);
		lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CONTAIN);
}

inline void system_screen_layout(lv_obj_t* screen, lv_obj_t* content,
		lv_obj_t* logo_container, lv_obj_t* logo_image, lv_obj_t* const* items, size_t count) {
		const int gap = lv_font_get_line_height(
				lv_obj_get_style_text_font(items[0], LV_PART_MAIN)) / 2;
		int text_height = 0;
		for (size_t i = 0; i < count; ++i) {
				lv_obj_t* item = items[i];
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
						// Size the object as well as the image so layout uses its visible bounds.
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
		for (size_t i = 0; i < count; ++i) {
				lv_obj_t* item = items[i];
				if (!item || lv_obj_has_flag(item, LV_OBJ_FLAG_HIDDEN)) continue;
				lv_obj_align(item, LV_ALIGN_TOP_MID, 0, y);
				y += lv_obj_get_height(item) + gap;
		}
		if (y) y -= gap;
		lv_obj_set_height(content, y);
		if (y + 16 <= lv_obj_get_height(screen)) {
				lv_obj_center(content);
		} else {
				lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 8);
		}
}

#endif

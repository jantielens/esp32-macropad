#include "epaper_overlay.h"

#if HAS_EPAPER

#include "config_manager.h"
#include "epaper_battery.h"
#include "epaper_driver.h"
#include "log_manager.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

namespace {

constexpr uint8_t kItemBattIcon  = 0x1;
constexpr uint8_t kItemBattPct   = 0x2;
constexpr uint8_t kItemTimestamp = 0x4;
constexpr uint8_t kItemCycleTime = 0x8;

uint8_t resolve_color(uint8_t color_id) {
		switch (color_id) {
				case 0: return EPAPER_BLACK;
				case 1: return EPAPER_DARK_GRAY;
				case 2: return EPAPER_LIGHT_GRAY;
				case 3: return EPAPER_WHITE;
				default: return EPAPER_BLACK;
		}
}

// Tiny battery icon: outline + fill proportional to percentage. ~28×14 px.
void draw_battery_icon(int16_t x, int16_t y, uint8_t pct, uint8_t color, uint8_t bg) {
		const int16_t bw = 28, bh = 14, cap_w = 3, cap_h = 6;
		// Body
		epaper_driver_fill_rect(x, y, bw, bh, bg);
		epaper_driver_draw_rect(x, y, bw, bh, color);
		// Cap
		epaper_driver_fill_rect(x + bw, y + (bh - cap_h) / 2, cap_w, cap_h, color);
		// Fill
		int16_t fill_w = ((int)(bw - 4) * pct + 50) / 100;
		if (fill_w < 0) fill_w = 0;
		if (fill_w > bw - 4) fill_w = bw - 4;
		if (fill_w > 0) {
				epaper_driver_fill_rect(x + 2, y + 2, fill_w, bh - 4, color);
		}
}

} // namespace

void epaper_overlay_render(const DeviceConfig* config, uint16_t battery_mv, uint32_t cycle_time_ms) {
		if (!config || !config->epaper_overlay_enabled) return;
		if (config->epaper_overlay_items == 0) return;

		// Build the line of text first so we can measure it for the background pad.
		char text[64];
		text[0] = '\0';
		size_t len = 0;
		auto append = [&](const char* s) {
				if (!s) return;
				size_t n = strlen(s);
				if (len + n + 1 >= sizeof(text)) return;
				memcpy(text + len, s, n);
				len += n;
				text[len] = '\0';
		};

		const uint8_t pct = epaper_battery_percent(battery_mv);
		char chunk[24];

		if (config->epaper_overlay_items & kItemBattPct) {
				snprintf(chunk, sizeof(chunk), "%u%%", (unsigned)pct);
				append(chunk);
		}
		if (config->epaper_overlay_items & kItemTimestamp) {
				const time_t now = time(nullptr);
				struct tm tm_buf;
				if (now > 1704067200 && localtime_r(&now, &tm_buf)) {
						snprintf(chunk, sizeof(chunk), "%s%02d:%02d",
								 len ? "  " : "", tm_buf.tm_hour, tm_buf.tm_min);
						append(chunk);
				}
		}
		if (config->epaper_overlay_items & kItemCycleTime) {
				snprintf(chunk, sizeof(chunk), "%s%ums", len ? "  " : "", (unsigned)cycle_time_ms);
				append(chunk);
		}

		const bool show_icon = (config->epaper_overlay_items & kItemBattIcon) != 0;
		const bool show_text = (len > 0);
		if (!show_icon && !show_text) return;

		epaper_driver_set_font(EPAPER_FONT_MEDIUM);
		const uint8_t color = resolve_color(config->epaper_overlay_color);
		// Background uses the opposite extreme so the overlay stays legible
		// regardless of what's underneath. Light text on dark bg, or vice versa.
		const uint8_t bg = (color == EPAPER_BLACK || color == EPAPER_DARK_GRAY)
				? EPAPER_WHITE : EPAPER_BLACK;

		int16_t tx1 = 0, ty1 = 0; uint16_t tw = 0, th = 0;
		if (show_text) {
				epaper_driver_get_text_bounds(text, 0, 0, &tx1, &ty1, &tw, &th);
		}

		const int16_t pad = 6;
		const int16_t icon_w = show_icon ? 28 : 0;
		const int16_t icon_h = show_icon ? 14 : 0;
		const int16_t gap = (show_icon && show_text) ? 6 : 0;
		const int16_t content_w = icon_w + gap + (int16_t)tw;
		const int16_t content_h = (icon_h > (int16_t)th) ? icon_h : (int16_t)th;
		const int16_t box_w = content_w + pad * 2;
		const int16_t box_h = content_h + pad * 2;

		const int16_t W = epaper_driver_width();
		const int16_t H = epaper_driver_height();
		const int16_t margin = 12;

		int16_t bx = 0, by = 0;
		switch (config->epaper_overlay_position) {
				case 0: bx = margin; by = margin; break;                       // TL
				case 1: bx = W - margin - box_w; by = margin; break;           // TR
				case 2: bx = margin; by = H - margin - box_h; break;           // BL
				default: bx = W - margin - box_w; by = H - margin - box_h; break; // BR
		}
		if (bx < 0) bx = 0;
		if (by < 0) by = 0;

		// Background pad (no outline — the rounded fill on its own reads as a
		// chip; an outline made the overlay feel boxed-in on small panels).
		epaper_driver_fill_round_rect(bx, by, box_w, box_h, 8, bg);

		int16_t cx = bx + pad;
		const int16_t cy = by + (box_h - content_h) / 2;

		if (show_icon) {
				const int16_t icon_y = cy + (content_h - icon_h) / 2;
				draw_battery_icon(cx, icon_y, pct, color, bg);
				cx += icon_w + gap;
		}
		if (show_text) {
				// getTextBounds returns ty1 relative to baseline, so the cursor y
				// is content_top - ty1.
				epaper_driver_set_text_color(color);
				epaper_driver_set_cursor(cx - tx1, cy - ty1);
				epaper_driver_print(text);
		}

		LOGI("Epaper", "Overlay drawn (pos=%u items=0x%02x batt=%u%% cycle=%ums)",
			 (unsigned)config->epaper_overlay_position,
			 (unsigned)config->epaper_overlay_items,
			 (unsigned)pct, (unsigned)cycle_time_ms);
}

#endif // HAS_EPAPER

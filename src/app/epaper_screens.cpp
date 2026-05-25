#include "epaper_screens.h"

#if HAS_EPAPER

#include "epaper_driver.h"
#include "log_manager.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

void draw_centered(const char* text, int16_t y) {
		int16_t x1, y1; uint16_t tw, th;
		epaper_driver_get_text_bounds(text, 0, y, &x1, &y1, &tw, &th);
		const int16_t w = epaper_driver_width();
		const int16_t x = ((int16_t)w - (int16_t)tw) / 2 - x1;
		epaper_driver_set_cursor(x, y);
		epaper_driver_print(text);
}

// Decorative border so the static screens read as intentional UI rather than
// "the dashboard image failed to load" garbage. 16 px inset, dark gray.
void draw_border() {
		const int16_t w = epaper_driver_width();
		const int16_t h = epaper_driver_height();
		const int16_t inset = 16;
		epaper_driver_draw_round_rect(inset, inset, w - inset * 2, h - inset * 2, 24, EPAPER_DARK_GRAY);
}

} // namespace

void epaper_screen_boot_splash(const char* device_name, const char* version) {
		epaper_driver_clear();
		draw_border();

		const int16_t h = epaper_driver_height();
		const int16_t cy = h / 2;

		epaper_driver_set_text_color(EPAPER_BLACK);

		epaper_driver_set_font(EPAPER_FONT_LARGE);
		draw_centered(device_name && *device_name ? device_name : "Inkplate", cy - 24);

		epaper_driver_set_font(EPAPER_FONT_MEDIUM);
		char buf[80];
		snprintf(buf, sizeof(buf), "Starting up — firmware %s", version && *version ? version : "(unknown)");
		draw_centered(buf, cy + 24);

		epaper_driver_set_font(EPAPER_FONT_MEDIUM);
		epaper_driver_set_text_color(EPAPER_DARK_GRAY);
		draw_centered("Connecting to Wi-Fi and fetching latest image…", cy + 64);

		LOGI("Epaper", "Boot splash drawn (device='%s' version='%s')", device_name ? device_name : "", version ? version : "");
}

void epaper_screen_config_mode(const char* ssid, const char* ip, bool is_ap) {
		epaper_driver_clear();
		draw_border();

		const int16_t h = epaper_driver_height();
		int16_t y = h / 2 - 80;

		epaper_driver_set_text_color(EPAPER_BLACK);
		epaper_driver_set_font(EPAPER_FONT_LARGE);
		draw_centered("Configuration Mode", y);
		y += 50;

		epaper_driver_set_font(EPAPER_FONT_MEDIUM);
		char buf[128];
		if (is_ap) {
				snprintf(buf, sizeof(buf), "Connect to Wi-Fi network:");
				draw_centered(buf, y); y += 32;
				snprintf(buf, sizeof(buf), "%s", ssid ? ssid : "");
				epaper_driver_set_font(EPAPER_FONT_LARGE);
				draw_centered(buf, y); y += 50;
				epaper_driver_set_font(EPAPER_FONT_MEDIUM);
				draw_centered("then browse to:", y); y += 32;
				snprintf(buf, sizeof(buf), "http://%s/", ip && *ip ? ip : "192.168.4.1");
				epaper_driver_set_font(EPAPER_FONT_LARGE);
				draw_centered(buf, y);
		} else {
				snprintf(buf, sizeof(buf), "Wi-Fi: %s", ssid ? ssid : "");
				draw_centered(buf, y); y += 32;
				draw_centered("Browse to:", y); y += 32;
				snprintf(buf, sizeof(buf), "http://%s/", ip && *ip ? ip : "?.?.?.?");
				epaper_driver_set_font(EPAPER_FONT_LARGE);
				draw_centered(buf, y);
		}

		LOGI("Epaper", "Config screen drawn (mode=%s ssid='%s' ip='%s')",
			 is_ap ? "AP" : "STA", ssid ? ssid : "", ip ? ip : "");
}

void epaper_screen_error(const char* error_detail, uint32_t retry_seconds) {
		epaper_driver_clear();
		draw_border();

		const int16_t h = epaper_driver_height();
		int16_t y = h / 2 - 40;

		epaper_driver_set_text_color(EPAPER_BLACK);
		epaper_driver_set_font(EPAPER_FONT_LARGE);
		draw_centered("Refresh failed", y);
		y += 50;

		epaper_driver_set_font(EPAPER_FONT_MEDIUM);
		draw_centered(error_detail && *error_detail ? error_detail : "Unknown error", y);
		y += 40;

		char buf[64];
		if (retry_seconds > 0) {
				if (retry_seconds < 120) {
						snprintf(buf, sizeof(buf), "Retrying in %us", (unsigned)retry_seconds);
				} else {
						snprintf(buf, sizeof(buf), "Retrying in %u min", (unsigned)(retry_seconds / 60));
				}
				epaper_driver_set_text_color(EPAPER_DARK_GRAY);
				draw_centered(buf, y);
		} else {
				epaper_driver_set_text_color(EPAPER_DARK_GRAY);
				draw_centered("Press the wake button to retry", y);
		}

		LOGW("Epaper", "Error screen drawn: %s (retry=%us)",
			 error_detail ? error_detail : "(null)", (unsigned)retry_seconds);
}

void epaper_screen_low_battery(uint16_t mv, uint8_t pct) {
		epaper_driver_clear();
		draw_border();

		const int16_t h = epaper_driver_height();
		int16_t y = h / 2 - 40;

		epaper_driver_set_text_color(EPAPER_BLACK);
		epaper_driver_set_font(EPAPER_FONT_LARGE);
		draw_centered("Low Battery", y);
		y += 50;

		epaper_driver_set_font(EPAPER_FONT_MEDIUM);
		char buf[64];
		snprintf(buf, sizeof(buf), "%u%%  (%u.%02u V)", (unsigned)pct,
				 (unsigned)(mv / 1000u), (unsigned)((mv % 1000u) / 10u));
		draw_centered(buf, y);
		y += 32;
		epaper_driver_set_text_color(EPAPER_DARK_GRAY);
		draw_centered("Please charge the device.", y);

		LOGW("Epaper", "Low-battery screen drawn (mv=%u pct=%u)", (unsigned)mv, (unsigned)pct);
}

#endif // HAS_EPAPER

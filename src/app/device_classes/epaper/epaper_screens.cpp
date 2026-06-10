#include "epaper_screens.h"

#if HAS_EPAPER

#include "epaper_driver.h"
#include "log_manager.h"

#include <Arduino.h>
#include <WString.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// StatusScreenBuilder
// ---------------------------------------------------------------------------
// Builder used by every full-screen status screen on the e-paper panel. Each
// screen describes its content as a sequence of typed lines (heading, body,
// muted note, explicit spacer) and the builder handles:
//   - clear + decorative border
//   - per-font line-height measurement via getTextBounds("Aygj")
//   - vertical centring of the whole stack
//   - horizontal centring of each line via getTextBounds on the actual text
//
// Adding a new screen = a few chained calls in a new top-level function.
// The 16-line cap is far more than any current or planned screen needs and
// keeps the builder stack-allocated.
// ---------------------------------------------------------------------------

namespace {

constexpr int16_t BORDER_INSET    = 16;
constexpr int16_t LINE_GAP_PX     = 8;   // baseline-to-baseline padding between lines of same group
constexpr int16_t DEFAULT_SPACER  = 16;  // extra px added by `.spacer()` with no argument

void draw_border() {
		const int16_t w = epaper_driver_width();
		const int16_t h = epaper_driver_height();
		epaper_driver_draw_round_rect(BORDER_INSET, BORDER_INSET,
		                              w - BORDER_INSET * 2, h - BORDER_INSET * 2,
		                              24, EPAPER_DARK_GRAY);
}

int16_t font_line_height(uint8_t font_id) {
		epaper_driver_set_font(font_id);
		int16_t x1, y1; uint16_t w, h;
		// Mixed ascenders + descenders give the true visual line height for
		// the current font, independent of the strings being drawn.
		epaper_driver_get_text_bounds("Aygj", 0, 0, &x1, &y1, &w, &h);
		return (int16_t)h;
}

void draw_centered_baseline(const char* text, int16_t baseline_y) {
		int16_t x1, y1; uint16_t w, h;
		epaper_driver_get_text_bounds(text, 0, baseline_y, &x1, &y1, &w, &h);
		const int16_t panel_w = epaper_driver_width();
		const int16_t x = (panel_w - (int16_t)w) / 2 - x1;
		epaper_driver_set_cursor(x, baseline_y);
		epaper_driver_print(text);
}

class StatusScreenBuilder {
public:
		StatusScreenBuilder& heading1(const char* t) { return add(t, EPAPER_FONT_LARGE,  EPAPER_BLACK); }
		StatusScreenBuilder& heading2(const char* t) { return add(t, EPAPER_FONT_MEDIUM, EPAPER_BLACK); }
		StatusScreenBuilder& text(const char* t)     { return add(t, EPAPER_FONT_MEDIUM, EPAPER_BLACK); }
		StatusScreenBuilder& muted(const char* t)    { return add(t, EPAPER_FONT_MEDIUM, EPAPER_DARK_GRAY); }
		StatusScreenBuilder& spacer(int16_t px = DEFAULT_SPACER) {
				if (count_ > 0) lines_[count_ - 1].gap_after += px;
				return *this;
		}

		void draw() const {
				epaper_driver_clear();
				draw_border();
				if (count_ == 0) {
						return;
				}

				// Pass 1: measure total stacked height.
				int16_t heights[kMax] = {0};
				int16_t total = 0;
				for (uint8_t i = 0; i < count_; ++i) {
						heights[i] = font_line_height(lines_[i].font);
						total += heights[i];
						if (i + 1 < count_) {
								total += LINE_GAP_PX + lines_[i].gap_after;
						}
				}

				// Pass 2: draw centred. `baseline` starts at the baseline of the
				// first line; subsequent lines advance by their own height plus
				// the inter-line gap (and any explicit spacer added after the
				// previous line).
				const int16_t panel_h = epaper_driver_height();
				int16_t baseline = (panel_h - total) / 2 + heights[0];
				for (uint8_t i = 0; i < count_; ++i) {
						epaper_driver_set_font(lines_[i].font);
						epaper_driver_set_text_color(lines_[i].color);
						draw_centered_baseline(lines_[i].text.c_str(), baseline);
						if (i + 1 < count_) {
								baseline += heights[i + 1] + LINE_GAP_PX + lines_[i].gap_after;
						}
				}
		}

private:
		static constexpr uint8_t kMax = 12;

		struct Line {
				String  text;
				uint8_t font;
				uint8_t color;
				int16_t gap_after;
		};

		Line    lines_[kMax];
		uint8_t count_ = 0;

		StatusScreenBuilder& add(const char* t, uint8_t font, uint8_t color) {
				if (count_ >= kMax) {
						return *this;
				}
				Line& l = lines_[count_++];
				l.text      = t ? t : "";
				l.font      = font;
				l.color     = color;
				l.gap_after = 0;
				return *this;
		}
};

} // namespace

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

void epaper_screen_boot_splash(const char* device_name, const char* version) {
		char ver[80];
		snprintf(ver, sizeof(ver), "Firmware %s", version && *version ? version : "(unknown)");

		StatusScreenBuilder()
				.heading1(device_name && *device_name ? device_name : "E-Paper")
				.spacer()
				.text(ver)
				.muted("Connecting to Wi-Fi and fetching latest image…")
				.draw();

		LOGI("Epaper", "Boot splash drawn (device='%s' version='%s')",
		     device_name ? device_name : "", version ? version : "");
}

void epaper_screen_manual_refresh(const char* message) {
		StatusScreenBuilder()
				.heading1("Refreshing")
				.spacer()
				.text(message && *message ? message : "Button pressed — fetching latest image…")
				.draw();

		LOGI("Epaper", "Manual-refresh screen drawn");
}

void epaper_screen_config_mode_starting() {
		StatusScreenBuilder()
				.heading1("Configuration Mode")
				.spacer()
				.text("Preparing Wi-Fi setup…")
				.muted("This takes a few seconds.")
				.draw();

		LOGI("Epaper", "Config-mode starting screen drawn");
}

void epaper_screen_config_mode(const char* ssid, const char* ip, bool is_ap) {
		char url[80];
		snprintf(url, sizeof(url), "http://%s/", ip && *ip ? ip : (is_ap ? "192.168.4.1" : "?.?.?.?"));

		if (is_ap) {
				StatusScreenBuilder()
						.heading1("Configuration Mode")
						.spacer()
						.text("Connect to Wi-Fi network:")
						.heading1(ssid && *ssid ? ssid : "")
						.spacer()
						.text("then browse to:")
						.heading1(url)
						.spacer()
						.muted("Press the wake button to exit and resume normal mode.")
						.draw();
		} else {
				char wifi_line[96];
				snprintf(wifi_line, sizeof(wifi_line), "Wi-Fi: %s", ssid && *ssid ? ssid : "");
				StatusScreenBuilder()
						.heading1("Configuration Mode")
						.spacer()
						.text(wifi_line)
						.text("Browse to:")
						.heading1(url)
						.spacer()
						.muted("Press the wake button to exit and resume normal mode.")
						.draw();
		}

		LOGI("Epaper", "Config screen drawn (mode=%s ssid='%s' ip='%s')",
		     is_ap ? "AP" : "STA", ssid ? ssid : "", ip ? ip : "");
}

void epaper_screen_returning_to_normal() {
		StatusScreenBuilder()
				.heading1("Returning to Normal Mode")
				.spacer()
				.text("Rebooting…")
				.draw();

		LOGI("Epaper", "Returning-to-normal screen drawn");
}

void epaper_screen_error(const char* error_detail, uint32_t retry_seconds) {
		StatusScreenBuilder b;
		b.heading1("Refresh failed")
		 .spacer()
		 .text(error_detail && *error_detail ? error_detail : "Unknown error");

		if (retry_seconds > 0) {
				char buf[64];
				if (retry_seconds < 120) {
						snprintf(buf, sizeof(buf), "Retrying in %us", (unsigned)retry_seconds);
				} else {
						snprintf(buf, sizeof(buf), "Retrying in %u min", (unsigned)(retry_seconds / 60));
				}
				b.muted(buf);
		} else {
				b.muted("Press the wake button to retry");
		}
		b.draw();

		LOGW("Epaper", "Error screen drawn: %s (retry=%us)",
		     error_detail ? error_detail : "(null)", (unsigned)retry_seconds);
}

void epaper_screen_low_battery(uint16_t mv, uint8_t pct) {
		char level[64];
		snprintf(level, sizeof(level), "%u%%  (%u.%02u V)", (unsigned)pct,
		         (unsigned)(mv / 1000u), (unsigned)((mv % 1000u) / 10u));

		StatusScreenBuilder()
				.heading1("Low Battery")
				.spacer()
				.text(level)
				.muted("Please charge the device.")
				.draw();

		LOGW("Epaper", "Low-battery screen drawn (mv=%u pct=%u)", (unsigned)mv, (unsigned)pct);
}

#endif // HAS_EPAPER

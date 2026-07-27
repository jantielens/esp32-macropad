// Inkplate e-paper driver (Soldered InkplateLibrary, 3-bit grayscale boards).
//
// Shared across the Inkplate 3-bit-grayscale targets (Inkplate 5 V2 and
// Inkplate 6FLICK): both are driven through the same InkplateLibrary
// `Inkplate(INKPLATE_3BIT)` code path, expose the same TPS65186 PMIC for VCOM,
// and use the same Adafruit_GFX primitives — only the panel resolution differs
// (the library auto-detects it from the selected board). Only compiled when the
// board is one of these Inkplate targets and HAS_EPAPER is enabled. Wired into
// the build through device_classes/epaper/epaper_drivers.cpp.

#include "board_config.h"

#if HAS_EPAPER && (defined(BOARD_INKPLATE5V2) || defined(BOARD_INKPLATE6FLICK))

#include "device_classes/epaper/epaper_driver.h"
#include "device_classes/epaper/epaper_http.h"
#include "log_manager.h"

#include <Inkplate.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <math.h>

// Bundled Inter fonts. Only included in this implementation .cpp so the
// PROGMEM bitmap arrays do not multiply across translation units.
#include "fonts/Inter_Regular_8pt7b.h"
#include "fonts/Inter_Regular_12pt7b.h"
#include "fonts/Inter_Bold_20pt7b.h"

// Per-board font overrides. A board_overrides.h can `#define` any of these to
// point at a different GFXfont* (and `#include` the corresponding font header
// from its own headers) to retune the e-paper status screens for a different
// pixel density. Defaults below match the original Inkplate 5 V2 tuning.
#ifndef EPAPER_FONT_SMALL_PTR
#define EPAPER_FONT_SMALL_PTR  (&Inter_Regular8pt7b)
#endif
#ifndef EPAPER_FONT_MEDIUM_PTR
#define EPAPER_FONT_MEDIUM_PTR (&Inter_Regular12pt7b)
#endif
#ifndef EPAPER_FONT_LARGE_PTR
#define EPAPER_FONT_LARGE_PTR  (&Inter_Bold20pt7b)
#endif

// The Inkplate constructor allocates large 3-bit framebuffers in PSRAM and
// touches the I2C / SPI peripherals. Doing that during C++ global init runs
// before the Arduino runtime (heap_caps PSRAM mapping, peripheral clocks) is
// ready and silently SW_RESETs the chip with no Serial output. Construct the
// instance lazily on first `epaper_driver_begin()` call from `setup()`.
static Inkplate *s_display = nullptr;
static bool s_began = false;

static const GFXfont* const s_font_table[3] = {
		EPAPER_FONT_SMALL_PTR,   // EPAPER_FONT_SMALL
		EPAPER_FONT_MEDIUM_PTR,  // EPAPER_FONT_MEDIUM
		EPAPER_FONT_LARGE_PTR,   // EPAPER_FONT_LARGE
};

bool epaper_driver_begin() {
		if (s_began) return true;
		if (!s_display) {
				s_display = new Inkplate(INKPLATE_3BIT);
				if (!s_display) {
						LOGE("Epaper", "Inkplate allocation failed");
						return false;
				}
		}
		s_display->begin();
		s_display->clearDisplay();
		s_began = true;
#if defined(BOARD_INKPLATE6FLICK)
		LOGI("Epaper", "Inkplate 6FLICK panel initialized (3-bit grayscale)");
#else
		LOGI("Epaper", "Inkplate 5 V2 panel initialized (3-bit grayscale)");
#endif
		return true;
}

// No background-init path on this board: begin_async() runs begin() inline and
// join() returns the cached result. Battery sense is gated behind begin() (the
// Inkplate library reads it through the panel), so the duty-cycle hook must
// power the panel up before reading the cell.
static bool s_begin_result = false;
void epaper_driver_begin_async() { s_begin_result = epaper_driver_begin(); }
bool epaper_driver_begin_join() { return s_begin_result; }
bool epaper_driver_battery_ready_before_begin() { return false; }

void epaper_driver_set_rotation(uint8_t rotation) {
		if (!s_began || !s_display) return;
		s_display->setRotation(rotation & 0x3);
}

bool epaper_driver_draw_url(const char* url) {
		if (!s_began || !s_display || !url || !*url) return false;
		// Download the image ourselves (the shared epaper_http_download avoids the
		// InkplateLibrary HTTPS downloader, which crashes on https:// hosts — see
		// device_classes/epaper/epaper_http.h), then sniff the format from the magic
		// bytes. We cannot use InkplateLibrary's `image.draw(url, ...)` because it
		// picks the decoder from the URL's file extension, which extensionless
		// static image endpoints may not provide.
		uint8_t* buf = nullptr;
		size_t len = 0;
		if (!epaper_http_download(url, &buf, &len)) {
				LOGW("Epaper", "download failed for %s", url);
				return false;
		}

		bool ok = false;
		if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
				ok = s_display->image.drawJpegFromBuffer(buf, (int32_t)len, 0, 0, true /*dither*/, false /*invert*/);
		} else if (buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G') {
				ok = s_display->image.drawPngFromBuffer(buf, (int32_t)len, 0, 0, true /*dither*/, false /*invert*/);
		} else if (buf[0] == 'B' && buf[1] == 'M') {
				ok = s_display->image.drawBitmapFromBuffer(buf, 0, 0, true /*dither*/, false /*invert*/);
		} else {
				LOGW("Epaper", "unknown image format for %s (magic %02X %02X %02X %02X)",
				     url, buf[0], buf[1], buf[2], buf[3]);
		}

		heap_caps_free(buf);
		if (!ok) {
				LOGW("Epaper", "decode/draw failed for %s", url);
		}
		return ok;
}

bool epaper_driver_display() {
		if (!s_began || !s_display) return false;
		s_display->display();
		return true;
}

void epaper_driver_sleep() {
		if (!s_began || !s_display) return;
		s_display->einkOff();
}

uint16_t epaper_driver_battery_mv() {
		if (!s_began || !s_display) return 0;
		const double v = s_display->readBattery();
		if (v <= 0.0) return 0;
		return (uint16_t)(v * 1000.0);
}

// SD image cache is unsupported on this board (no shared-bus SD slot); the
// SD-cache HAL vtable resolves to the inline no-ops in epaper_sd_cache.h.

// ---------------------------------------------------------------------------
// GFX primitives — pass-through to Adafruit_GFX methods inherited by Inkplate.
// ---------------------------------------------------------------------------

void epaper_driver_clear() {
		if (!s_began || !s_display) return;
		s_display->clearDisplay();
}

void epaper_driver_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
		if (!s_began || !s_display) return;
		s_display->fillRect(x, y, w, h, color);
}

void epaper_driver_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
		if (!s_began || !s_display) return;
		s_display->drawRect(x, y, w, h, color);
}

void epaper_driver_draw_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color) {
		if (!s_began || !s_display) return;
		s_display->drawRoundRect(x, y, w, h, r, color);
}

void epaper_driver_fill_round_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint8_t color) {
		if (!s_began || !s_display) return;
		s_display->fillRoundRect(x, y, w, h, r, color);
}

void epaper_driver_set_font(uint8_t font_id) {
		if (!s_began || !s_display) return;
		if (font_id > EPAPER_FONT_LARGE) font_id = EPAPER_FONT_MEDIUM;
		s_display->setFont(s_font_table[font_id]);
}

void epaper_driver_set_text_color(uint8_t color) {
		if (!s_began || !s_display) return;
		s_display->setTextColor(color);
}

void epaper_driver_set_cursor(int16_t x, int16_t y) {
		if (!s_began || !s_display) return;
		s_display->setCursor(x, y);
}

void epaper_driver_print(const char* text) {
		if (!s_began || !s_display || !text) return;
		s_display->print(text);
}

void epaper_driver_get_text_bounds(const char* text, int16_t x, int16_t y,
                                   int16_t* x1, int16_t* y1,
                                   uint16_t* w, uint16_t* h) {
		if (!s_began || !s_display || !text) {
				if (x1) *x1 = 0;
				if (y1) *y1 = 0;
				if (w) *w = 0;
				if (h) *h = 0;
				return;
		}
		s_display->getTextBounds(text, x, y, x1, y1, w, h);
}

int16_t epaper_driver_width() {
		if (!s_began || !s_display) return 0;
		return s_display->width();
}

int16_t epaper_driver_height() {
		if (!s_began || !s_display) return 0;
		return s_display->height();
}

// ---------------------------------------------------------------------------
// VCOM management (TPS65186 PMIC at I²C address 0x48). The volatile VCOM
// registers are 0x03 (LSB, 8 bits) and 0x04 (bit 0 = MSB, bit 6 = "program
// EEPROM" trigger). Stored value is unsigned 0..511 representing |VCOM| in
// hundredths of a volt; the panel-side value is always negative.
// ---------------------------------------------------------------------------

static constexpr uint8_t TPS65186_ADDR = 0x48;

static int vcom_read_raw_locked() {
		Wire.beginTransmission(TPS65186_ADDR);
		Wire.write((uint8_t)0x03);
		Wire.endTransmission(false);
		Wire.requestFrom((uint8_t)TPS65186_ADDR, (uint8_t)1);
		if (!Wire.available()) return -1;
		uint8_t lsb = Wire.read();

		Wire.beginTransmission(TPS65186_ADDR);
		Wire.write((uint8_t)0x04);
		Wire.endTransmission(false);
		Wire.requestFrom((uint8_t)TPS65186_ADDR, (uint8_t)1);
		if (!Wire.available()) return -1;
		uint8_t msb = Wire.read() & 0x01;

		return ((int)msb << 8) | lsb;
}

float epaper_driver_read_vcom() {
		if (!s_began || !s_display) return NAN;
		Wire.begin();
		s_display->einkOn();
		delay(10);
		int raw = vcom_read_raw_locked();
		s_display->einkOff();
		delay(10);
		if (raw < 0) {
				LOGW("Epaper", "VCOM read failed (I²C)");
				return NAN;
		}
		return -((float)raw / 100.0f);
}

bool epaper_driver_write_vcom(float vcom) {
		if (!s_began || !s_display) return false;
		// Reference programPanelVCOM() bound: -5.0 V .. 0.0 V (panel needs
		// negative VCOM; 0 and positive are rejected to avoid damage).
		if (!(vcom < 0.0f && vcom >= -5.0f)) {
				LOGW("Epaper", "VCOM write out of range: %.3f", vcom);
				return false;
		}
		int raw = (int)(-vcom * 100.0f + 0.5f);
		if (raw < 0 || raw > 511) {
				LOGW("Epaper", "VCOM raw out of range: %d", raw);
				return false;
		}
		uint8_t lsb = raw & 0xFF;
		uint8_t msb = (raw >> 8) & 0x01;

		Wire.begin();
		s_display->einkOn();
		delay(10);

		LOGI("Epaper", "VCOM program: %.3f V (raw=%d)", vcom, raw);

		// Write LSB
		Wire.beginTransmission(TPS65186_ADDR);
		Wire.write((uint8_t)0x03);
		Wire.write(lsb);
		Wire.endTransmission();

		// Preserve other bits in 0x04; only update bit 0
		Wire.beginTransmission(TPS65186_ADDR);
		Wire.write((uint8_t)0x04);
		Wire.endTransmission(false);
		Wire.requestFrom((uint8_t)TPS65186_ADDR, (uint8_t)1);
		uint8_t r4 = Wire.available() ? Wire.read() : 0;
		r4 = (r4 & ~0x01) | msb;
		Wire.beginTransmission(TPS65186_ADDR);
		Wire.write((uint8_t)0x04);
		Wire.write(r4);
		Wire.endTransmission();

		// Trigger EEPROM program (bit 6) — auto-clears when done.
		Wire.beginTransmission(TPS65186_ADDR);
		Wire.write((uint8_t)0x04);
		Wire.write(r4 | (1 << 6));
		Wire.endTransmission();

		const uint32_t start = millis();
		bool programmed = false;
		while (millis() - start < 1000) {
				delay(10);
				Wire.beginTransmission(TPS65186_ADDR);
				Wire.write((uint8_t)0x04);
				Wire.endTransmission(false);
				Wire.requestFrom((uint8_t)TPS65186_ADDR, (uint8_t)1);
				uint8_t status = Wire.available() ? Wire.read() : 0xFF;
				if ((status & (1 << 6)) == 0) {
						programmed = true;
						break;
				}
		}
		if (!programmed) {
				LOGW("Epaper", "VCOM EEPROM program bit did not clear within 1s");
		}

		// Zero volatile registers so the power cycle reload reflects EEPROM.
		Wire.beginTransmission(TPS65186_ADDR);
		Wire.write((uint8_t)0x03);
		Wire.write((uint8_t)0);
		Wire.endTransmission();
		Wire.beginTransmission(TPS65186_ADDR);
		Wire.write((uint8_t)0x04);
		Wire.write((uint8_t)0);
		Wire.endTransmission();

		// Power-cycle TPS65186 so it reloads VCOM from EEPROM.
		s_display->einkOff();
		delay(100);
		s_display->einkOn();
		delay(100);

		int check = vcom_read_raw_locked();
		s_display->einkOff();
		delay(10);

		if (check != raw) {
				LOGW("Epaper", "VCOM verify mismatch: wrote %d, read %d", raw, check);
				return false;
		}
		LOGI("Epaper", "VCOM programmed successfully: %.3f V", vcom);
		return true;
}

void epaper_driver_show_vcom_test_pattern(float preview_vcom) {
		if (!s_began || !s_display) return;

		// Decide which VCOM the panel will actually drive while we display the
		// pattern. A finite, in-range `preview_vcom` is written to the volatile
		// TPS65186 registers only — the EEPROM is left untouched so the value
		// reverts on the next power cycle. This lets users A/B test candidate
		// values without burning EEPROM cycles, matching the upstream
		// inkplate-dashboard "test VCOM" flow.
		float displayed_vcom;
		const bool preview_mode = !isnan(preview_vcom)
				&& preview_vcom < 0.0f && preview_vcom >= -5.0f;
		if (preview_mode) {
				const int raw = (int)(-preview_vcom * 100.0f + 0.5f);
				const uint8_t lsb = raw & 0xFF;
				const uint8_t msb = (raw >> 8) & 0x01;

				Wire.begin();
				s_display->einkOn();
				delay(10);

				// Write volatile LSB.
				Wire.beginTransmission(TPS65186_ADDR);
				Wire.write((uint8_t)0x03);
				Wire.write(lsb);
				Wire.endTransmission();

				// Preserve other bits in 0x04; only update MSB (bit 0). Do NOT set
				// bit 6 — that would commit to EEPROM.
				Wire.beginTransmission(TPS65186_ADDR);
				Wire.write((uint8_t)0x04);
				Wire.endTransmission(false);
				Wire.requestFrom((uint8_t)TPS65186_ADDR, (uint8_t)1);
				uint8_t r4 = Wire.available() ? Wire.read() : 0;
				r4 = (r4 & ~0x01) | msb;
				Wire.beginTransmission(TPS65186_ADDR);
				Wire.write((uint8_t)0x04);
				Wire.write(r4);
				Wire.endTransmission();

				displayed_vcom = preview_vcom;
				LOGI("Epaper", "VCOM preview: %.3f V (volatile only, EEPROM untouched)",
					 preview_vcom);
		} else {
				// Use the EEPROM-programmed value. read_vcom power-cycles internally;
				// that's fine — we're about to redraw the whole panel.
				displayed_vcom = epaper_driver_read_vcom();
		}

		s_display->clearDisplay();
		const int16_t w = s_display->width();
		const int16_t h = s_display->height();

		// 8 vertical bars, one per gray level. Reserve top strip for label.
		const int16_t label_h = 48;
		const int16_t bar_top = label_h;
		const int16_t bar_h = h - bar_top;
		const int16_t bar_w = w / 8;
		for (int i = 0; i < 8; ++i) {
				s_display->fillRect(i * bar_w, bar_top, bar_w, bar_h, (uint8_t)i);
		}
		// Last bar takes any remainder so we don't leave a sliver.
		if (8 * bar_w < w) {
				s_display->fillRect(7 * bar_w, bar_top, w - 7 * bar_w, bar_h, 7);
		}

		// Centered label in EPAPER_FONT_MEDIUM against a white strip so it's
		// legible regardless of where the eye lands. We draw on the top region
		// which is currently the cleared default color.
		char buf[48];
		if (isnan(displayed_vcom)) {
				snprintf(buf, sizeof(buf), "VCOM: N/A");
		} else if (preview_mode) {
				snprintf(buf, sizeof(buf), "VCOM: %.2f V (preview)", displayed_vcom);
		} else {
				snprintf(buf, sizeof(buf), "VCOM: %.2f V", displayed_vcom);
		}
		s_display->setFont(s_font_table[EPAPER_FONT_MEDIUM]);
		s_display->setTextColor(EPAPER_BLACK);
		int16_t x1, y1; uint16_t tw, th;
		s_display->getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
		const int16_t cx = (w - (int16_t)tw) / 2;
		const int16_t cy = (label_h - (int16_t)th) / 2 - y1;
		s_display->setCursor(cx, cy);
		s_display->print(buf);

		s_display->display();
		s_display->einkOff();
}

// ---------------------------------------------------------------------------
// Frontlight (only compiled when the board enables HAS_EPAPER_FRONTLIGHT).
// Inkplate 5V2 has no frontlight, so this block is inert there.
// ---------------------------------------------------------------------------

#if HAS_EPAPER_FRONTLIGHT
void epaper_driver_frontlight_on(uint8_t brightness) {
		if (!s_began || !s_display || brightness == 0) return;
		if (brightness > 63) brightness = 63;
		// Anti-flicker sequence (matches the upstream Inkplate dashboard
		// reference): dim to 0 first, then enable the boost circuit, then
		// ramp to the target brightness. Skipping the dim-to-0 step before
		// frontlight(true) produces a visible flash on cold start.
		s_display->setFrontlight(0);
		s_display->frontlight(true);
		s_display->setFrontlight(brightness);
}

void epaper_driver_frontlight_off() {
		if (!s_began || !s_display) return;
		s_display->setFrontlight(0);
		s_display->frontlight(false);
}
#endif // HAS_EPAPER_FRONTLIGHT

#endif // HAS_EPAPER && (BOARD_INKPLATE5V2 || BOARD_INKPLATE6FLICK)

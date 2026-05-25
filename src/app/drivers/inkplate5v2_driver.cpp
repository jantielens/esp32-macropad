// Inkplate 5 V2 e-paper driver (Soldered InkplateLibrary).
//
// Only compiled when the board is Inkplate 5V2 and HAS_EPAPER is enabled.
// Wired into the build through src/app/epaper_drivers.cpp.

#include "board_config.h"

#if HAS_EPAPER && defined(BOARD_INKPLATE5V2)

#include "epaper_driver.h"
#include "log_manager.h"

#include <Inkplate.h>

// The Inkplate constructor allocates large 3-bit framebuffers in PSRAM and
// touches the I2C / SPI peripherals. Doing that during C++ global init runs
// before the Arduino runtime (heap_caps PSRAM mapping, peripheral clocks) is
// ready and silently SW_RESETs the chip with no Serial output. Construct the
// instance lazily on first `epaper_driver_begin()` call from `setup()`.
static Inkplate *s_display = nullptr;
static bool s_began = false;

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
		LOGI("Epaper", "Inkplate 5 V2 panel initialized (3-bit grayscale)");
		return true;
}

void epaper_driver_set_rotation(uint8_t rotation) {
		if (!s_began || !s_display) return;
		s_display->setRotation(rotation & 0x3);
}

bool epaper_driver_draw_url(const char* url) {
		if (!s_began || !s_display || !url || !*url) return false;
		// InkplateLibrary v11 exposes the auto-format web image loader via the
		// `display.image` member (Inkplate inherits Image as an aggregated sub-
		// object on this board, not via base class). Signature:
		//   bool Image::draw(const char *url, int x, int y, bool dither, bool invert)
		const bool ok = s_display->image.draw(url, 0, 0, true /*dither*/, false /*invert*/);
		if (!ok) {
				LOGW("Epaper", "image.draw failed for %s", url);
		}
		return ok;
}

void epaper_driver_display() {
		if (!s_began || !s_display) return;
		s_display->display();
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

#endif // HAS_EPAPER && BOARD_INKPLATE5V2

/*
 * Arduino_GFX ST77916 QSPI Display Driver
 *
 * Wraps Arduino_GFX library for the ST77916 QSPI 360x360 round display
 * (JC3636W518 board).
 *
 * Unlike the AXS15231B, the ST77916 (Sitronix) retains address window
 * registers across QSPI CS toggles.  This allows true direct rendering:
 * each LVGL flush strip is sent to the panel via draw16bitRGBBitmap()
 * without needing a PSRAM framebuffer.
 */

#ifndef ARDUINO_GFX_ST77916_DRIVER_H
#define ARDUINO_GFX_ST77916_DRIVER_H

#include "../display_driver.h"
#include "../board_config.h"
#include <Arduino_GFX_Library.h>

class Arduino_GFX_ST77916_Driver : public DisplayDriver {
private:
		Arduino_DataBus* bus;
		Arduino_GFX* gfx;
		uint8_t currentBrightness;
		bool backlightPwmAttached;

		// Current drawing area (set by setAddrWindow, used by pushColors)
		int16_t currentX, currentY;
		uint16_t currentW, currentH;

public:
		Arduino_GFX_ST77916_Driver();
		~Arduino_GFX_ST77916_Driver() override;

		void init() override;
		void setRotation(uint8_t rotation) override;
		int width() override;
		int height() override;
		void setBacklight(bool on) override;
		void setBacklightBrightness(uint8_t brightness) override;
		uint8_t getBacklightBrightness() override;
		bool hasBacklightControl() override;
		void applyDisplayFixes() override;

		void startWrite() override;
		void endWrite() override;
		void setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) override;
		void pushColors(uint16_t* data, uint32_t len, bool swap_bytes = true) override;

		// Direct render mode — each LVGL strip sent immediately to panel.
		RenderMode renderMode() const override { return RenderMode::Direct; }
};

#endif // ARDUINO_GFX_ST77916_DRIVER_H

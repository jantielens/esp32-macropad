/*
 * Arduino_GFX Display Driver
 * 
 * Wrapper for moononournation's Arduino_GFX library.
 * Supports QSPI displays like AXS15231B (JC3248W535).
 *
 * QSPI partial-write limitation:
 *   The ESP32 QSPI bus toggles CS between each SPI transaction.
 *   CASET/RASET (address window) and pixel data travel in separate
 *   CS sessions, so the panel loses the address pointer — every
 *   writePixels lands at (0,0) regardless of the window coordinates.
 *   fillScreen works because (0,0) happens to be the correct origin
 *   for a full-screen write.
 *
 * Solution: keep a portrait-orientation PSRAM framebuffer.
 *   pushColors() copies (with optional rotation) each LVGL flush
 *   strip into the framebuffer and tracks which portrait rows were
 *   touched.  present() sends rows 0..maxDirtyRow to the panel
 *   via draw16bitRGBBitmap(0, 0, fb, w, dirtyRows) — starting at
 *   (0,0) which matches the panel's actual write position.
 *   For partial redraws (widget animations, status updates) this
 *   transfers significantly less data than a full-frame flush.
 *
 * Compared with the former Arduino_Canvas approach this eliminates
 * the Canvas object (and its full GFX drawing API overhead) while
 * keeping the same reliable full-frame transfer.
 */

#ifndef ARDUINO_GFX_DRIVER_H
#define ARDUINO_GFX_DRIVER_H

#include "../display_driver.h"
#include "../board_config.h"
#include <Arduino_GFX_Library.h>

class Arduino_GFX_Driver : public DisplayDriver {
private:
		Arduino_DataBus* bus;
		Arduino_GFX* gfx;
		uint8_t currentBrightness;  // Current brightness level (0-100%)
		bool backlightPwmAttached;
		uint16_t displayWidth;      // Physical panel width (portrait)
		uint16_t displayHeight;     // Physical panel height (portrait)
		uint8_t displayRotation;
		
		// Current drawing area (set by setAddrWindow, used by pushColors)
		int16_t currentX, currentY;
		uint16_t currentW, currentH;
		
		// PSRAM framebuffer: portrait-orientation (displayWidth × displayHeight).
		// pushColors() writes into this; present() sends it to the panel.
		uint16_t* framebuffer;
		
		// Dirty-row tracking: rows 0..dirtyMaxRow are sent in present(),
		// skipping everything below the lowest dirty row.
		bool hasDirtyRows;
		uint16_t dirtyMaxRow;
		
public:
		Arduino_GFX_Driver();
		~Arduino_GFX_Driver() override;
		
		void init() override;
		void setRotation(uint8_t rotation) override;
		int width() override;
		int height() override;
		void setBacklight(bool on) override;
		void setBacklightBrightness(uint8_t brightness) override;  // 0-100%
		uint8_t getBacklightBrightness() override;
		bool hasBacklightControl() override;
		void applyDisplayFixes() override;
		
		void startWrite() override;
		void endWrite() override;
		void setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) override;
		void pushColors(uint16_t* data, uint32_t len, bool swap_bytes = true) override;
		
		// Buffered render mode — present() flushes the full framebuffer to the panel.
		RenderMode renderMode() const override { return RenderMode::Buffered; }
		void present() override;
};

#endif // ARDUINO_GFX_DRIVER_H

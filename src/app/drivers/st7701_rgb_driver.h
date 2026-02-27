/*
 * ST7701 RGB Display Driver
 * 
 * Driver for ST7701-based RGB parallel displays (e.g., ESP32-4848S040).
 * Delegates all panel management to Arduino_GFX library, matching the
 * exact code path used by proven working samples.
 * 
 * Hardware interface:
 * - 9-bit SPI bus for ST7701 initialization commands (CS, SCK, MOSI)
 * - 16-bit RGB parallel data bus (5R + 6G + 5B + DE/HSYNC/VSYNC/PCLK)
 * 
 * Panel lifecycle (Arduino_GFX managed):
 * - Arduino_SWSPI: 9-bit SPI bus for ST7701 register init
 * - Arduino_ESP32RGBPanel: ESP-IDF RGB panel + framebuffer + bounce buffer
 * - Arduino_RGB_Display: draw API + cache writeback (auto_flush=true)
 * 
 * LVGL flush path: setAddrWindow → pushColors → draw16bitRGBBitmap()
 * (identical to Arduino_GFX sample projects)
 * 
 * Reference: GUITION ESP32-S3-4848S040 (480×480 IPS RGB panel)
 */

#ifndef ST7701_RGB_DRIVER_H
#define ST7701_RGB_DRIVER_H

#include "../display_driver.h"
#include "../board_config.h"
#include <Arduino_GFX_Library.h>

// Forward declarations (Arduino_GFX classes)
class Arduino_ESP32RGBPanel;
class Arduino_RGB_Display;

class ST7701_RGB_Driver : public DisplayDriver {
private:
		Arduino_DataBus* bus;                    // 9-bit SPI bus for ST7701 commands
		Arduino_ESP32RGBPanel* rgbpanel;         // RGB panel bus (ESP-IDF panel wrapper)
		Arduino_RGB_Display* gfx;               // Display draw API (framebuffer + cache mgmt)
		uint8_t currentBrightness;              // Current brightness level (0-100%)
		uint16_t displayWidth;
		uint16_t displayHeight;
		uint8_t displayRotation;
		
		// Backlight control
		bool backlightOn;
		
		// Current flush area (for pushColors implementation)
		int16_t flushX, flushY;
		uint16_t flushW, flushH;
		
public:
		ST7701_RGB_Driver();
		~ST7701_RGB_Driver() override;
		
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
};

#endif // ST7701_RGB_DRIVER_H

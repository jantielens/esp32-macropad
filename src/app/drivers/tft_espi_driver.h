/*
 * TFT_eSPI Display Driver
 * 
 * Wrapper for Bodmer's TFT_eSPI library.
 * Supports: ILI9341, ST7789, ST7735, ILI9488, and many others.
 */

#ifndef TFT_ESPI_DRIVER_H
#define TFT_ESPI_DRIVER_H

#include "../display_driver.h"
#include "../board_config.h"
#include <TFT_eSPI.h>

class TFT_eSPI_Driver : public DisplayDriver {
private:
		TFT_eSPI tft;
		uint8_t currentBrightness;  // Current brightness level (0-100%)
		
public:
		TFT_eSPI_Driver();
		~TFT_eSPI_Driver() override = default;
		
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

#endif // TFT_ESPI_DRIVER_H

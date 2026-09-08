/*
 * Arduino_GFX NV3041A QSPI Display Driver
 *
 * Direct LVGL flush driver for the 480x272 NV3041A panel fitted to the
 * hardware-untested JC4827W543C board.
 */

#ifndef ARDUINO_GFX_NV3041A_DRIVER_H
#define ARDUINO_GFX_NV3041A_DRIVER_H

#include "../display_driver.h"
#include "../board_config.h"
#include <Arduino_GFX_Library.h>

class Arduino_GFX_NV3041A_Driver : public DisplayDriver {
private:
		Arduino_DataBus* bus;
		Arduino_GFX* gfx;
		uint8_t currentBrightness;
		int16_t currentX;
		int16_t currentY;
		uint16_t currentW;
		uint16_t currentH;

public:
		Arduino_GFX_NV3041A_Driver();
		~Arduino_GFX_NV3041A_Driver() override;

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

		RenderMode renderMode() const override { return RenderMode::Direct; }

		void displaySleep() override;
		void displayWake() override;
		void displayWakeSleepOut() override;
		void displayWakeDisplayOn() override;
};

#endif // ARDUINO_GFX_NV3041A_DRIVER_H
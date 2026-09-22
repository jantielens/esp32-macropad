#ifndef INKPLATE6FLICK_LVGL_DRIVER_H
#define INKPLATE6FLICK_LVGL_DRIVER_H

#include "../display_driver.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Inkplate;

Inkplate* inkplate6flick_lvgl_instance();

class Inkplate6Flick_LVGL_Driver : public DisplayDriver {
public:
		Inkplate6Flick_LVGL_Driver();
		~Inkplate6Flick_LVGL_Driver() override;

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
		void pushColors(uint16_t* data, uint32_t len, bool swap_bytes) override;

		RenderMode renderMode() const override { return RenderMode::Buffered; }
		void present() override;

private:
		Inkplate* display;
		SemaphoreHandle_t framebufferMutex;
		int16_t currentX;
		int16_t currentY;
		uint16_t currentW;
		uint16_t currentH;
		uint8_t rotation;
		uint32_t lastRefreshMs;
		bool pendingChanges;

		void writePixel(int16_t x, int16_t y, uint16_t rgb565);
};

#endif
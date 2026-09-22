#ifndef INKPLATE6FLICK_LVGL_DRIVER_H
#define INKPLATE6FLICK_LVGL_DRIVER_H

#include "../display_driver.h"
#include "../config_manager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Inkplate;

Inkplate* inkplate6flick_lvgl_instance();

class Inkplate6Flick_LVGL_Driver : public DisplayDriver {
public:
		Inkplate6Flick_LVGL_Driver(DeviceConfig* config);
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
		bool requestFullRefresh() override;
		int presentationMode() const override { return panelMode; }

private:
		Inkplate* display;
		DeviceConfig* config;
		SemaphoreHandle_t framebufferMutex;
		int16_t currentX;
		int16_t currentY;
		uint16_t currentW;
		uint16_t currentH;
		uint8_t rotation;
		uint8_t panelMode;
		uint32_t lastRefreshMs;
		uint32_t lastChangeMs;
		uint8_t* presentedFramebuffer;
		uint32_t refreshCount;
		uint32_t partialUpdateCount;
		uint16_t bwPartialUpdatesSinceFull;
		uint32_t noOpSkipCount;
		uint8_t backlightBrightness;
		uint8_t frontlightLevel;
		bool pendingChanges;
		bool frontlightEnabled;
		portMUX_TYPE fullRefreshRequestMux = portMUX_INITIALIZER_UNLOCKED;
		bool fullRefreshRequested;
		bool hasPresentedFrame;

		void writePixel(int16_t x, int16_t y, uint16_t rgb565);
		bool usesBwMode() const;
		uint8_t* framebuffer() const;
		size_t framebufferBytes() const;
		bool fullRefreshPending();
		bool consumeFullRefreshRequest();
};

#endif
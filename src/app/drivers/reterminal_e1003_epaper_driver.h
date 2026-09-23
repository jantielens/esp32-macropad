#ifndef RETERMINAL_E1003_EPAPER_DRIVER_H
#define RETERMINAL_E1003_EPAPER_DRIVER_H

#include "../config_manager.h"
#include "../display_driver.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class ReTerminalE1003EpaperDriver : public DisplayDriver {
public:
		ReTerminalE1003EpaperDriver(DeviceConfig* config);
		~ReTerminalE1003EpaperDriver() override;

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
		bool isAvailable() const override { return initialized; }
		int presentationMode() const override { return panelMode; }

private:
		DeviceConfig* config;
		int16_t currentX;
		int16_t currentY;
		uint16_t currentW;
		uint16_t currentH;
		uint8_t rotation;
		uint8_t panelMode;
		uint32_t lastRefreshMs;
		uint32_t lastChangeMs;
		uint32_t bwPartialUpdatesSinceFull;
		uint8_t* drawingFramebuffer;
		uint8_t* presentedFramebuffer;
		SemaphoreHandle_t framebufferMutex;
		portMUX_TYPE fullRefreshRequestMux = portMUX_INITIALIZER_UNLOCKED;
		bool pendingChanges;
		bool fullRefreshRequested;
		bool hasPresentedFrame;
		bool initialized;
		bool dirtyRegionValid;
		uint16_t dirtyX;
		uint16_t dirtyY;
		uint16_t dirtyX2;
		uint16_t dirtyY2;

		void writePixel(int16_t x, int16_t y, uint16_t rgb565);
		void markDirtyRegion(int16_t x, int16_t y, uint16_t w, uint16_t h);
		bool usesBwMode() const;
		size_t framebufferBytes() const;
		bool fullRefreshPending();
		bool consumeFullRefreshRequest();
};

#endif // RETERMINAL_E1003_EPAPER_DRIVER_H